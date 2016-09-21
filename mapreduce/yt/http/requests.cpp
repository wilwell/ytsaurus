#include "requests.h"

#include "error.h"
#include "transaction.h"

#include <mapreduce/yt/common/log.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/helpers.h>

#include <library/json/json_reader.h>

#include <util/random//normal.h>
#include <util/stream/file.h>
#include <util/string/printf.h>
#include <util/generic/buffer.h>
#include <util/generic/ymath.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

bool ParseBoolFromResponse(const Stroka& response)
{
    return GetBool(NodeFromYsonString(response));
}

TGUID ParseGuidFromResponse(const Stroka& response)
{
    auto node = NodeFromYsonString(response);
    return GetGuid(node.AsString());
}

void ParseJsonStringArray(const Stroka& response, yvector<Stroka>& result)
{
    NJson::TJsonValue value;
    TStringInput input(response);
    NJson::ReadJsonTree(&input, &value);

    const NJson::TJsonValue::TArray& array = value.GetArray();
    result.clear();
    result.reserve(array.size());
    for (size_t i = 0; i < array.size(); ++i) {
        result.push_back(array[i].GetString());
    }
}

////////////////////////////////////////////////////////////////////////////////

TTransactionId StartTransaction(
    const TAuth& auth,
    const TTransactionId& parentId,
    const TMaybe<TDuration>& timeout,
    bool pingAncestors,
    const TMaybe<TNode>& attributes)
{
    THttpHeader header("POST", "start_tx");
    header.AddTransactionId(parentId);

    header.AddMutationId();
    header.AddParam("timeout",
        (timeout ? timeout : TConfig::Get()->TxTimeout)->MilliSeconds());
    if (pingAncestors) {
        header.AddParam("ping_ancestor_transactions", "true");
    }
    if (attributes) {
        header.SetParameters(AttributesToYsonString(*attributes));
    }

    auto txId = ParseGuidFromResponse(RetryRequest(auth, header));
    LOG_INFO("Transaction %s started", ~GetGuidAsString(txId));
    return txId;
}

void TransactionRequest(
    const TAuth& auth,
    const Stroka& command,
    const TTransactionId& transactionId,
    std::function<void()> errorCallback = {})
{
    THttpHeader header("POST", command);
    header.AddTransactionId(transactionId);
    header.AddMutationId();
    RetryRequest(auth, header, "", false, false, errorCallback);
}

void PingTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    try {
        TransactionRequest(auth, "ping_tx", transactionId);
    } catch (yexception&) {
        // ignore all ping errors
    }
}

void AbortTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    TransactionRequest(auth, "abort_tx", transactionId);

    LOG_INFO("Transaction %s aborted", ~GetGuidAsString(transactionId));
}

void CommitTransaction(
    const TAuth& auth,
    const TTransactionId& transactionId)
{
    TransactionRequest(auth, "commit_tx", transactionId,
        [&] () { PingTransaction(auth, transactionId); });

    LOG_INFO("Transaction %s commited", ~GetGuidAsString(transactionId));
}

////////////////////////////////////////////////////////////////////////////////

Stroka Get(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path)
{
    THttpHeader header("GET", "get");
    header.AddTransactionId(transactionId);
    header.AddPath(path);
    return RetryRequest(auth, header);
}

void Set(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const Stroka& value)
{
    THttpHeader header("PUT", "set");
    header.AddTransactionId(transactionId);
    header.AddPath(path);
    header.AddMutationId();
    RetryRequest(auth, header, value);
}

bool Exists(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path)
{
    THttpHeader header("GET", "exists");
    header.AddTransactionId(transactionId);
    header.AddPath(path);
    return ParseBoolFromResponse(RetryRequest(auth, header));
}

void Create(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const Stroka& type,
    bool ignoreExisting,
    bool recursive)
{
    THttpHeader header("POST", "create");
    header.AddTransactionId(transactionId);
    header.AddPath(path);
    header.AddParam("type", type);
    header.AddParam("ignore_existing", ignoreExisting);
    header.AddParam("recursive", recursive);
    header.AddMutationId();
    RetryRequest(auth, header);
}

void Lock(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TYPath& path,
    const Stroka& mode)
{
    THttpHeader header("POST", "lock");
    header.AddTransactionId(transactionId);
    header.AddPath(path);
    header.AddParam("mode", mode);
    header.AddMutationId();
    RetryRequest(auth, header);
}

////////////////////////////////////////////////////////////////////////////////

Stroka GetProxyForHeavyRequest(const TAuth& auth)
{
    if (!TConfig::Get()->UseHosts) {
        return auth.ServerName;
    }

    yvector<Stroka> hosts;
    while (hosts.empty()) {
        THttpHeader header("GET", TConfig::Get()->Hosts, false);
        Stroka response = RetryRequest(auth, header);
        ParseJsonStringArray(response, hosts);
        if (hosts.empty()) {
            Sleep(TConfig::Get()->RetryInterval);
        }
    }
    if (hosts.size() < 3) {
        return hosts.front();
    }
    size_t hostIdx = -1;
    do {
        hostIdx = Abs<double>(NormalRandom<double>(0, hosts.size() / 2));
    } while (hostIdx >= hosts.size());

    return hosts[hostIdx];
}

Stroka RetryRequest(
    const TAuth& auth,
    THttpHeader& header,
    const Stroka& body,
    bool isHeavy,
    bool isOperation,
    std::function<void()> errorCallback)
{
    int retryCount = isOperation ?
        TConfig::Get()->StartOperationRetryCount :
        TConfig::Get()->RetryCount;

    header.SetToken(auth.Token);

    TDuration socketTimeout = (header.GetCommand() == "ping_tx") ?
        TConfig::Get()->PingTimeout : TDuration::Zero();

    bool needMutationId = false;
    bool needRetry = false;

    for (int attempt = 0; attempt < retryCount; ++attempt) {
        Stroka response;

        Stroka hostName(auth.ServerName);
        if (isHeavy) {
            hostName = GetProxyForHeavyRequest(auth);
        }

        bool hasError = false;
        TDuration retryInterval;

        THttpRequest request(hostName);
        auto requestId = request.GetRequestId();

        if (needMutationId) {
            header.AddMutationId();
            needMutationId = false;
            needRetry = false;
        }

        if (needRetry) {
            header.AddParam("retry", "true");
        } else {
            header.RemoveParam("retry");
            needRetry = true;
        }

        try {
            request.Connect(socketTimeout);
            try {
                TOutputStream* output = request.StartRequest(header);
                output->Write(body);
                request.FinishRequest();
            } catch (yexception&) {
                // try to read error in response
            }
            response = request.GetResponse();

        } catch (TErrorResponse& e) {
            LOG_ERROR("RSP %s - attempt %d failed",
                ~requestId,
                attempt);

            if (!e.IsRetriable() || attempt + 1 == retryCount) {
                throw;
            }
            if (e.IsConcurrentOperationsLimitReached()) {
                needMutationId = true;
            }

            hasError = true;
            retryInterval = e.GetRetryInterval();

        } catch (yexception& e) {
            LOG_ERROR("RSP %s - %s - attempt %d failed",
                ~requestId,
                e.what(),
                attempt);

            request.InvalidateConnection();
            if (attempt + 1 == retryCount) {
                throw;
            }
            hasError = true;
            retryInterval = TConfig::Get()->RetryInterval;
        }

        if (!hasError) {
            return response;
        }

        if (errorCallback) {
            errorCallback();
        }

        Sleep(retryInterval);
    }

    ythrow yexception() << "unreachable";
}

void RetryHeavyWriteRequest(
    const TAuth& auth,
    const TTransactionId& parentId,
    THttpHeader& header,
    std::function<THolder<TInputStream>()> streamMaker)
{
    int retryCount = TConfig::Get()->RetryCount;
    header.SetToken(auth.Token);

    for (int attempt = 0; attempt < retryCount; ++attempt) {
        TPingableTransaction attemptTx(auth, parentId);

        auto input = streamMaker();

        auto proxyName = GetProxyForHeavyRequest(auth);
        THttpRequest request(proxyName);
        auto requestId = request.GetRequestId();

        header.AddTransactionId(attemptTx.GetId());
        header.SetRequestCompression(TConfig::Get()->ContentEncoding);

        try {
            request.Connect();
            try {
                TOutputStream* output = request.StartRequest(header);
                TransferData(input.Get(), output);
                request.FinishRequest();
            } catch (yexception&) {
                // try to read error in response
            }
            request.GetResponse();

        } catch (TErrorResponse& e) {
            LOG_ERROR("RSP %s - attempt %d failed",
                ~requestId,
                attempt);

            if (!e.IsRetriable() || attempt + 1 == retryCount) {
                throw;
            }
            Sleep(e.GetRetryInterval());
            continue;

        } catch (yexception& e) {
            LOG_ERROR("RSP %s - %s - attempt %d failed",
                ~requestId,
                e.what(),
                attempt);

            request.InvalidateConnection();
            if (attempt + 1 == retryCount) {
                throw;
            }
            Sleep(TConfig::Get()->RetryInterval);
            continue;
        }

        attemptTx.Commit();
        return;
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
