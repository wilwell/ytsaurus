#include "stdafx.h"
#include "cypress_service.h"
#include "node_proxy.h"

#include "../ytree/ypath_detail.h"

namespace NYT {
namespace NCypress {

using namespace NBus;
using namespace NYTree;
using namespace NMetaState;
using namespace NTransaction;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = CypressLogger;

////////////////////////////////////////////////////////////////////////////////

TCypressService::TCypressService(
    IInvoker* invoker,
    TCypressManager* cypressManager,
    TTransactionManager* transactionManager,
    NRpc::IServer* server)
    : NRpc::TServiceBase(
        invoker,
        TCypressServiceProxy::GetServiceName(),
        CypressLogger.GetCategory())
    , CypressManager(cypressManager)
    , TransactionManager(transactionManager)
{
    YASSERT(cypressManager != NULL);
    YASSERT(server != NULL);

    RegisterMethod(RPC_SERVICE_METHOD_DESC(Execute));
    RegisterMethod(RPC_SERVICE_METHOD_DESC(GetNodeId));

    server->RegisterService(this);
}

void TCypressService::ValidateTransactionId(const TTransactionId& transactionId)
{
    if (transactionId != NullTransactionId &&
        TransactionManager->FindTransaction(transactionId) == NULL)
    {
        ythrow TServiceException(EErrorCode::NoSuchTransaction) << 
            Sprintf("Invalid transaction id (TransactionId: %s)", ~transactionId.ToString());
    }
}

////////////////////////////////////////////////////////////////////////////////

RPC_SERVICE_METHOD_IMPL(TCypressService, Execute)
{
    UNUSED(response);

    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());

    TYPath path;
    Stroka verb;
    ParseYPathRequestHeader(
        ~context->GetUntypedContext(),
        &path,
        &verb);

    context->SetRequestInfo("TransactionId: %s, Path: %s, Verb: %s",
        ~transactionId.ToString(),
        ~path,
        ~verb);

    ValidateTransactionId(transactionId);

    auto rootProxy = CypressManager->GetNodeProxy(RootNodeId, transactionId);
    auto rootService = IYPathService::FromNode(~rootProxy);

    IYPathService::TPtr tailService;
    TYPath tailPath;
    NavigateYPath(~rootService, path, false, &tailService, &tailPath);

    auto innerContext = UnwrapYPathRequest(
        ~context->GetUntypedContext(),
        tailPath,
        verb,
        Logger.GetCategory(),
        ~FromFunctor([=] (const TYPathResponseHandlerParam& param)
            {
                WrapYPathResponse(~context->GetUntypedContext(), ~param.Message);
                context->Reply(
                    param.Error.IsOK()
                    ? NRpc::EErrorCode::OK
                    : TCypressServiceProxy::EErrorCode::VerbError);
            }));

    tailService->Invoke(~innerContext);
}

RPC_SERVICE_METHOD_IMPL(TCypressService, GetNodeId)
{
    auto transactionId = TTransactionId::FromProto(request->GetTransactionId());
    Stroka path = request->GetPath();

    context->SetRequestInfo("TransactionId: %s, Path: %s",
        ~transactionId.ToString(),
        ~path);

    ValidateTransactionId(transactionId);

    auto rootProxy = CypressManager->GetNodeProxy(RootNodeId, transactionId);
    auto rootService = IYPathService::FromNode(~rootProxy);

    IYPathService::TPtr targetService;
    try {
        targetService = NavigateYPath(~rootService, path);
    } catch (...) {
        ythrow TServiceException(EErrorCode::NavigationError) << CurrentExceptionMessage();
    }

    auto* targetNode = dynamic_cast<ICypressNodeProxy*>(~targetService);
    auto id = targetNode == NULL ? NullNodeId : targetNode->GetNodeId();
    response->SetNodeId(id.ToProto());
    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NCypress
} // namespace NYT
