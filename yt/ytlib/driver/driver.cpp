#include "stdafx.h"
#include "driver.h"
#include "config.h"
#include "command.h"
#include "transaction_commands.h"
#include "cypress_commands.h"
#include "etc_commands.h"
#include "file_commands.h"
#include "table_commands.h"
#include "scheduler_commands.h"

#include <ytlib/actions/parallel_awaiter.h>

#include <ytlib/ytree/fluent.h>
#include <ytlib/ytree/forwarding_yson_consumer.h>
#include <ytlib/yson/parser.h>
#include <ytlib/ytree/ephemeral_node_factory.h>

#include <ytlib/rpc/scoped_channel.h>
#include <ytlib/rpc/retrying_channel.h>

#include <ytlib/meta_state/config.h>
#include <ytlib/meta_state/master_channel.h>

#include <ytlib/chunk_client/client_block_cache.h>

#include <ytlib/scheduler/config.h>
#include <ytlib/scheduler/scheduler_channel.h>

#include <ytlib/security_client/rpc_helpers.h>

namespace NYT {
namespace NDriver {

using namespace NYTree;
using namespace NYson;
using namespace NRpc;
using namespace NElection;
using namespace NTransactionClient;
using namespace NChunkClient;
using namespace NScheduler;
using namespace NFormats;
using namespace NSecurityClient;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = DriverLogger;

////////////////////////////////////////////////////////////////////////////////

TDriverRequest::TDriverRequest()
    : InputStream(nullptr)
    , OutputStream(nullptr)
{ }

////////////////////////////////////////////////////////////////////////////////

TCommandDescriptor IDriver::GetCommandDescriptor(const Stroka& commandName)
{
    auto descriptor = FindCommandDescriptor(commandName);
    YCHECK(descriptor);
    return descriptor.Get();
}

////////////////////////////////////////////////////////////////////////////////

class TDriver
    : public IDriver
{
private:
    class TCommandContext;
    typedef TIntrusivePtr<TCommandContext> TCommandContextPtr;

public:
    explicit TDriver(TDriverConfigPtr config)
        : Config(config)
    {
        YCHECK(config);

        LeaderChannel = CreateLeaderChannel(Config->Masters);
        MasterChannel = CreateMasterChannel(Config->Masters);

        // TODO(babenko): for now we use the same timeout both for masters and scheduler
        SchedulerChannel = CreateSchedulerChannel(
            Config->Masters->RpcTimeout,
            LeaderChannel);

        BlockCache = CreateClientBlockCache(Config->BlockCache);

        // Register all commands.
#define REGISTER(command, name, inDataType, outDataType, isVolatile, isHeavy) \
        RegisterCommand<command>(TCommandDescriptor(name, EDataType::inDataType, EDataType::outDataType, isVolatile, isHeavy));

        REGISTER(TStartTransactionCommand,  "start_tx",          Null,       Structured, true,  false);
        REGISTER(TPingTransactionCommand,   "ping_tx",           Null,       Null,       true,  false);
        REGISTER(TCommitTransactionCommand, "commit_tx",         Null,       Null,       true,  false);
        REGISTER(TAbortTransactionCommand,  "abort_tx",          Null,       Null,       true,  false);

        REGISTER(TCreateCommand,            "create",            Null,       Structured, true,  false);
        REGISTER(TRemoveCommand,            "remove",            Null,       Null,       true,  false);
        REGISTER(TSetCommand,               "set",               Structured, Null,       true,  false);
        REGISTER(TGetCommand,               "get",               Null,       Structured, false, false);
        REGISTER(TListCommand,              "list",              Null,       Structured, false, false);
        REGISTER(TLockCommand,              "lock",              Null,       Null,       true,  false);
        REGISTER(TCopyCommand,              "copy",              Null,       Structured, true,  false);
        REGISTER(TMoveCommand,              "move",              Null,       Null,       true,  false);
        REGISTER(TLinkCommand,              "link",              Null,       Structured, true,  false);
        REGISTER(TExistsCommand,            "exists",            Null,       Structured, false, false);

        REGISTER(TUploadCommand,            "upload",            Binary,     Null,       true,  true );
        REGISTER(TDownloadCommand,          "download",          Null,       Binary,     false, true );

        REGISTER(TWriteCommand,             "write",             Tabular,    Null,       true,  true );
        REGISTER(TReadCommand,              "read",              Null,       Tabular,    false, true );

        REGISTER(TMergeCommand,             "merge",             Null,       Structured, true,  false);
        REGISTER(TEraseCommand,             "erase",             Null,       Structured, true,  false);
        REGISTER(TMapCommand,               "map",               Null,       Structured, true,  false);
        REGISTER(TSortCommand,              "sort",              Null,       Structured, true,  false);
        REGISTER(TReduceCommand,            "reduce",            Null,       Structured, true,  false);
        REGISTER(TMapReduceCommand,         "map_reduce",        Null,       Structured, true,  false);
        REGISTER(TAbortOperationCommand,    "abort_op",          Null,       Null,       true,  false);
        REGISTER(TSuspendOperationCommand,  "suspend_op",        Null,       Null,       true,  false);
        REGISTER(TResumeOperationCommand,   "resume_op",         Null,       Null,       true,  false);

        REGISTER(TParseYPathCommand,        "parse_ypath",       Null,       Structured, false, false);

        REGISTER(TAddMemberCommand,         "add_member",        Null,       Null,       true,  false);
        REGISTER(TRemoveMemberCommand,      "remove_member",     Null,       Null,       true,  false);
        REGISTER(TCheckPersmissionCommand,  "check_permission",  Null,       Structured, false, false);
#undef REGISTER
    }

    virtual TFuture<TDriverResponse> Execute(const TDriverRequest& request) override
    {
        YCHECK(request.InputStream);
        YCHECK(request.OutputStream);

        TDriverResponse response;

        auto it = Commands.find(request.CommandName);
        if (it == Commands.end()) {
            return MakePromise(TDriverResponse(TError("Unknown command: %s", ~request.CommandName)));
        }

        LOG_INFO("Command started (Command: %s, User: %s)",
            ~request.CommandName,
            ~ToString(request.AuthenticatedUser));

        const auto& entry = it->second;

        try {
            auto masterChannel = LeaderChannel;
            if (request.AuthenticatedUser) {
                masterChannel = CreateAuthenticatedChannel(
                    masterChannel,
                    request.AuthenticatedUser.Get());
            }
            masterChannel = CreateScopedChannel(masterChannel);

            auto schedulerChannel = SchedulerChannel;
            if (request.AuthenticatedUser) {
                schedulerChannel = CreateAuthenticatedChannel(
                    schedulerChannel,
                    request.AuthenticatedUser.Get());
            }
            schedulerChannel = CreateScopedChannel(schedulerChannel);

            auto transactionManager = New<TTransactionManager>(
                Config->TransactionManager,
                masterChannel);

            // TODO(babenko): ReadFromFollowers is switched off
            auto context = New<TCommandContext>(
                this,
                entry.Descriptor,
                &request,
                std::move(masterChannel),
                std::move(schedulerChannel),
                std::move(transactionManager));

            auto command = entry.Factory.Run();
            command->Execute(context);

            auto response = context->GetAsyncResponse();
            return response.Apply(BIND(&TDriver::OnExecutionCompleted, context, request));
        } catch (const std::exception& ex) {
            return MakePromise(TDriverResponse(TError("Uncaught exception") << ex));
        }
    }

    virtual TNullable<TCommandDescriptor> FindCommandDescriptor(const Stroka& commandName) override
    {
        auto it = Commands.find(commandName);
        if (it == Commands.end()) {
            return Null;
        }
        return it->second.Descriptor;
    }

    virtual std::vector<TCommandDescriptor> GetCommandDescriptors() override
    {
        std::vector<TCommandDescriptor> result;
        result.reserve(Commands.size());
        FOREACH (const auto& pair, Commands) {
            result.push_back(pair.second.Descriptor);
        }
        return result;
    }

    virtual IChannelPtr GetMasterChannel() override
    {
        return LeaderChannel;
    }

    virtual IChannelPtr GetSchedulerChannel() override
    {
        return SchedulerChannel;
    }

private:
    TDriverConfigPtr Config;

    IChannelPtr LeaderChannel;
    IChannelPtr MasterChannel;
    IChannelPtr SchedulerChannel;
    IBlockCachePtr BlockCache;

    typedef TCallback< ICommandPtr() > TCommandFactory;

    struct TCommandEntry
    {
        TCommandDescriptor Descriptor;
        TCommandFactory Factory;
    };

    yhash_map<Stroka, TCommandEntry> Commands;

    template <class TCommand>
    void RegisterCommand(const TCommandDescriptor& descriptor)
    {
        TCommandEntry entry;
        entry.Descriptor = descriptor;
        entry.Factory = BIND([] () -> ICommandPtr {
            return New<TCommand>();
        });
        YCHECK(Commands.insert(std::make_pair(descriptor.CommandName, entry)).second);
    }

    static TFuture<TDriverResponse> OnExecutionCompleted(
        TCommandContextPtr context,
        const TDriverRequest& req,
        TDriverResponse rsp)
    {
        if (rsp.Error.IsOK()) {
            LOG_INFO("Command completed (Command: %s)", ~req.CommandName);
        } else {
            LOG_INFO(rsp.Error, "Command failed (Command: %s)", ~req.CommandName);
        }

        context->GetTransactionManager()->AsyncAbortAll();

        return context->TerminateChannels().Apply(BIND([=] () {
            return rsp;
        }));
    }

    class TCommandContext
        : public ICommandContext
    {
    public:
        TCommandContext(
            TDriver* driver,
            const TCommandDescriptor& descriptor,
            const TDriverRequest* request,
            IChannelPtr masterChannel,
            IChannelPtr schedulerChannel,
            TTransactionManagerPtr transactionManager)
            : Driver(driver)
            , Descriptor(descriptor)
            , Request(request)
            , MasterChannel(std::move(masterChannel))
            , SchedulerChannel(std::move(schedulerChannel))
            , TransactionManager(std::move(transactionManager))
            , SyncInputStream(CreateSyncInputStream(Request->InputStream))
            , SyncOutputStream(CreateSyncOutputStream(Request->OutputStream))
            , Response(NewPromise<TDriverResponse>())
        { }

        TFuture<void> TerminateChannels()
        {
            LOG_DEBUG("Terminating channels");

            TError error("Command context terminated");
            auto awaiter = New<TParallelAwaiter>();
            awaiter->Await(MasterChannel->Terminate(error));
            awaiter->Await(SchedulerChannel->Terminate(error));
            return awaiter->Complete();
        }

        virtual TDriverConfigPtr GetConfig() override
        {
            return Driver->Config;
        }

        virtual IChannelPtr GetMasterChannel() override
        {
            return MasterChannel;
        }

        virtual IChannelPtr GetSchedulerChannel() override
        {
            return SchedulerChannel;
        }

        virtual IBlockCachePtr GetBlockCache() override
        {
            return Driver->BlockCache;
        }

        virtual TTransactionManagerPtr GetTransactionManager() override
        {
            return TransactionManager;
        }

        virtual const TDriverRequest* GetRequest() override
        {
            return Request;
        }

        virtual void SetResponse(const TDriverResponse& response)
        {
            Response.Set(response);
        }

        virtual TYsonProducer CreateInputProducer() override
        {
            auto&& format = ConvertTo<TFormat>(
                Request->Arguments->FindChild("input_format"));
            return CreateProducerForFormat(
                std::move(format),
                Descriptor.InputType,
                ~SyncInputStream);
        }

        virtual std::unique_ptr<IYsonConsumer> CreateOutputConsumer() override
        {
            auto&& format = ConvertTo<TFormat>(
                Request->Arguments->FindChild("output_format"));
            return CreateConsumerForFormat(
                format,
                Descriptor.OutputType,
                ~SyncOutputStream);
        }

        TFuture<TDriverResponse> GetAsyncResponse()
        {
            return Response;
        }

    private:
        TDriver* Driver;
        TCommandDescriptor Descriptor;
        const TDriverRequest* Request;

        IChannelPtr MasterChannel;
        IChannelPtr SchedulerChannel;
        TTransactionManagerPtr TransactionManager;

        std::unique_ptr<TInputStream> SyncInputStream;
        std::unique_ptr<TOutputStream> SyncOutputStream;

        TPromise<TDriverResponse> Response;
    };
};

////////////////////////////////////////////////////////////////////////////////

IDriverPtr CreateDriver(TDriverConfigPtr config)
{
    return New<TDriver>(config);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDriver
} // namespace NYT
