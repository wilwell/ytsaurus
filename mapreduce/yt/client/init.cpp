#include "init.h"

#include <mapreduce/yt/http/requests.h>

#include <mapreduce/yt/interface/init.h>
#include <mapreduce/yt/interface/operation.h>

#include <mapreduce/yt/interface/logging/log.h>

#include <mapreduce/yt/io/job_reader.h>

#include <mapreduce/yt/common/abortable_registry.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/helpers.h>
#include <mapreduce/yt/common/wait_proxy.h>

#include <library/sighandler/async_signals_handler.h>

#include <util/folder/dirut.h>

#include <util/generic/singleton.h>

#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/string/type.h>

#include <util/system/env.h>
#include <util/system/thread.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace {

void WriteVersionToLog()
{
    LOG_INFO("Wrapper version: %s", TProcessState::Get()->ClientVersion.data());
}

static TNode SecureVaultContents; // safe

void InitializeSecureVault()
{
    SecureVaultContents = NodeFromYsonString(
        GetEnv("YT_SECURE_VAULT", "{}"));
}

}

////////////////////////////////////////////////////////////////////////////////

const TNode& GetJobSecureVault()
{
    return SecureVaultContents;
}

////////////////////////////////////////////////////////////////////////////////

class TAbnormalTerminator
{
public:
    TAbnormalTerminator() = default;

    static void SetErrorTerminationHandler()
    {
        if (Instance().OldHandler_ != nullptr) {
            return;
        }

        Instance().OldHandler_ = std::set_terminate(&TerminateHandler);

        SetAsyncSignalFunction(SIGINT, SignalHandler);
        SetAsyncSignalFunction(SIGTERM, SignalHandler);
    }

private:
    static TAbnormalTerminator& Instance()
    {
        return *Singleton<TAbnormalTerminator>();
    }

    static void* Invoke(void* opaque)
    {
        (*reinterpret_cast<std::function<void()>*>(opaque))();
        return nullptr;
    }

    static void TerminateWithTimeout(
        const TDuration& timeout,
        const std::function<void(void)>& exitFunction,
        const TString& logMessage)
    {
        std::function<void()> threadFun = [=] {
            LOG_INFO("%s", logMessage.data());
            NDetail::TAbortableRegistry::Get()->AbortAllAndBlockForever();
        };
        TThread thread(TThread::TParams(Invoke, &threadFun).SetName("aborter"));
        thread.Start();
        thread.Detach();

        Sleep(timeout);
        exitFunction();
    }

    static void SignalHandler(int signalNumber)
    {
        TerminateWithTimeout(
            TDuration::Seconds(5),
            std::bind(_exit, -signalNumber),
            TStringBuilder() << "Signal " << signalNumber << " received, aborting transactions. Waiting 5 seconds...");
    }

    static void TerminateHandler()
    {
        TerminateWithTimeout(
            TDuration::Seconds(5),
            [&] {
                if (Instance().OldHandler_) {
                    Instance().OldHandler_();
                } else {
                    abort();
                }
            },
            TStringBuilder() << "Terminate called, aborting transactions. Waiting 5 seconds...");
    }

private:
    std::terminate_handler OldHandler_ = nullptr;
};

////////////////////////////////////////////////////////////////////////////////

namespace NDetail {

EInitStatus& GetInitStatus()
{
    static EInitStatus initStatus = EInitStatus::NotInitialized;
    return initStatus;
}

void CommonInitialize(int argc, const char** argv)
{
    auto logLevelStr = to_lower(TConfig::Get()->LogLevel);
    ILogger::ELevel logLevel;

    if (!TryFromString(logLevelStr, logLevel)) {
        Cerr << "Invalid log level: " << TConfig::Get()->LogLevel << Endl;
        exit(1);
    }

    SetLogger(CreateStdErrLogger(logLevel));

    TProcessState::Get()->SetCommandLine(argc, argv);
}

void NonJobInitialize(const TInitializeOptions& options)
{
    if (FromString<bool>(GetEnv("YT_CLEANUP_ON_TERMINATION", "0")) || options.CleanupOnTermination_) {
        TAbnormalTerminator::SetErrorTerminationHandler();
    }
    if (options.WaitProxy_) {
        NDetail::TWaitProxy::Get()->SetProxy(options.WaitProxy_);
    }
    WriteVersionToLog();
}

void ExecJob(int argc, const char** argv, const TInitializeOptions& options)
{
    // Now we are definitely in job.
    // We take this setting from environment variable to be consistent with client code.
    TConfig::Get()->UseClientProtobuf = IsTrue(GetEnv("YT_USE_CLIENT_PROTOBUF", ""));

    TString jobType = argc >= 2 ? argv[1] : TString();
    if (argc != 5 || jobType != "--yt-map" && jobType != "--yt-reduce") {
        // We are inside job but probably using old API
        // (i.e. both NYT::Initialize and NMR::Initialize are called).
        WriteVersionToLog();
        return;
    }

    InitializeSecureVault();

    TString jobName(argv[2]);
    size_t outputTableCount = FromString<size_t>(argv[3]);
    NDetail::OutputTableCount = static_cast<i64>(outputTableCount);
    int hasState = FromString<int>(argv[4]);

    THolder<IInputStream> jobStateStream;
    if (hasState) {
        jobStateStream = new TIFStream("jobstate");
    } else {
        jobStateStream = new TBufferStream(0);
    }

    int ret = TJobFactory::Get()->GetJobFunction(jobName.data())(outputTableCount, *jobStateStream);
    if (options.JobOnExitFunction_) {
        (*options.JobOnExitFunction_)();
    }
    exit(ret);
}

} // namespace NDetail

////////////////////////////////////////////////////////////////////////////////

void JoblessInitialize(const TInitializeOptions& options)
{
    static const char* fakeArgv[] = {"unknown..."};
    NDetail::CommonInitialize(1, fakeArgv);
    NDetail::NonJobInitialize(options);
    NDetail::GetInitStatus() = NDetail::EInitStatus::JoblessInitialization;
}

void Initialize(int argc, const char* argv[], const TInitializeOptions& options)
{
    NDetail::CommonInitialize(argc, argv);

    NDetail::GetInitStatus() = NDetail::EInitStatus::FullInitialization;

    const bool isInsideJob = !GetEnv("YT_JOB_ID").empty();
    if (isInsideJob) {
        NDetail::ExecJob(argc, argv, options);
    } else {
        NDetail::NonJobInitialize(options);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
