#include "occupant.h"

#include "bootstrap_proxy.h"
#include "helpers.h"
#include "occupier.h"
#include "private.h"

#include <yt/yt/server/lib/election/election_manager.h>
#include <yt/yt/server/lib/election/distributed_election_manager.h>

#include <yt/yt/server/lib/hydra_common/changelog.h>
#include <yt/yt/server/lib/hydra_common/hydra_manager.h>
#include <yt/yt/server/lib/hydra_common/hydra_service.h>
#include <yt/yt/server/lib/hydra_common/snapshot.h>
#include <yt/yt/server/lib/hydra_common/changelog.h>
#include <yt/yt/server/lib/hydra_common/snapshot.h>
#include <yt/yt/server/lib/hydra_common/hydra_manager.h>
#include <yt/yt/server/lib/hydra_common/snapshot_store_thunk.h>
#include <yt/yt/server/lib/hydra_common/changelog_store_factory_thunk.h>
#include <yt/yt/server/lib/hydra_common/remote_changelog_store.h>
#include <yt/yt/server/lib/hydra_common/remote_snapshot_store.h>

#include <yt/yt/server/lib/hydra/distributed_hydra_manager.h>

#include <yt/yt/server/lib/hydra2/distributed_hydra_manager.h>

#include <yt/yt/server/lib/election/election_manager.h>
#include <yt/yt/server/lib/election/election_manager_thunk.h>
#include <yt/yt/server/lib/election/alien_cell_peer_channel_factory.h>

#include <yt/yt/server/lib/hive/hive_manager.h>
#include <yt/yt/server/lib/hive/mailbox.h>

#include <yt/yt/server/lib/transaction_supervisor/transaction_supervisor.h>
#include <yt/yt/server/lib/transaction_supervisor/transaction_participant_provider.h>

#include <yt/yt/server/lib/misc/profiling_helpers.h>

#include <yt/yt/ytlib/hive/cell_directory.h>

#include <yt/yt/server/node/cluster_node/bootstrap.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/ytlib/tablet_client/config.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>
#include <yt/yt/ytlib/api/native/config.h>

#include <yt/yt/ytlib/election/cell_manager.h>

#include <yt/yt/ytlib/hive/cluster_directory_synchronizer.h>

#include <yt/yt/ytlib/transaction_client/remote_cluster_timestamp_provider.h>

#include <yt/yt/client/api/connection.h>
#include <yt/yt/client/api/client.h>
#include <yt/yt/client/api/transaction.h>

#include <yt/yt/client/transaction_client/timestamp_provider.h>

#include <yt/yt/client/security_client/public.h>

#include <yt/yt/core/concurrency/fair_share_action_queue.h>
#include <yt/yt/core/concurrency/scheduler.h>
#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/misc/atomic_object.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/profiling/profile_manager.h>

#include <yt/yt/core/bus/tcp/dispatcher.h>

#include <yt/yt/core/rpc/response_keeper.h>
#include <yt/yt/core/rpc/server.h>

#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/virtual.h>
#include <yt/yt/core/ytree/helpers.h>

namespace NYT::NCellarAgent {

using namespace NApi;
using namespace NCellarNodeTrackerClient::NProto;
using namespace NConcurrency;
using namespace NElection;
using namespace NHiveClient;
using namespace NHiveServer;
using namespace NHydra;
using namespace NObjectClient;
using namespace NRpc;
using namespace NTabletClient;
using namespace NTransactionClient;
using namespace NTransactionSupervisor;
using namespace NYTree;
using namespace NYson;

using NHydra::EPeerState;

////////////////////////////////////////////////////////////////////////////////

static const auto& Profiler = CellarAgentProfiler;

////////////////////////////////////////////////////////////////////////////////

class TCellarOccupant
    : public ICellarOccupant
{
public:
    TCellarOccupant(
        TCellarOccupantConfigPtr config,
        ICellarBootstrapProxyPtr bootstrap,
        int index,
        const TCreateCellSlotInfo& createInfo,
        ICellarOccupierPtr occupier)
        : Config_(config)
        , Bootstrap_(bootstrap)
        , Occupier_(std::move(occupier))
        , Index_(index)
        , PeerId_(createInfo.peer_id())
        , CellDescriptor_(FromProto<TCellId>(createInfo.cell_id()))
        , CellBundleName_(createInfo.cell_bundle())
        , Options_(ConvertTo<TTabletCellOptionsPtr>(TYsonString(createInfo.options())))
        , Logger(GetLogger())
    {
        VERIFY_INVOKER_THREAD_AFFINITY(GetOccupier()->GetOccupierAutomatonInvoker(), AutomatonThread);
    }

    ICellarOccupierPtr GetOccupier() const override
    {
        return Occupier_.Load();
    }

    int GetIndex() const override
    {
        return Index_;
    }

    TCellId GetCellId() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return CellDescriptor_.CellId;
    }

    EPeerState GetControlState() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (Finalizing_) {
            YT_LOG_DEBUG("Peer is finalized (CellId: %v, State: %v)",
                GetCellId(),
                EPeerState::Stopped);
            return EPeerState::Stopped;
        }

        if (auto hydraManager = GetHydraManager()) {
            return hydraManager->GetControlState();
        }

        if (Initialized_) {
            YT_LOG_DEBUG("Peer is not initialized yet (CellId: %v, State: %v)",
                GetCellId(),
                EPeerState::Stopped);
            return EPeerState::Stopped;
        }

        return EPeerState::None;
    }

    EPeerState GetAutomatonState() const override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto hydraManager = GetHydraManager();
        return hydraManager ? hydraManager->GetAutomatonState() : EPeerState::None;
    }

    TPeerId GetPeerId() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return PeerId_;
    }

    const TCellDescriptor& GetCellDescriptor() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return CellDescriptor_;
    }

    int GetConfigVersion() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return ConfigVersion_;
    }

    const IDistributedHydraManagerPtr GetHydraManager() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return HydraManager_.Load();
    }

    const IResponseKeeperPtr& GetResponseKeeper() const override
    {
        return ResponseKeeper_;
    }

    const TCompositeAutomatonPtr& GetAutomaton() const override
    {
        return Automaton_;
    }

    const THiveManagerPtr& GetHiveManager() const override
    {
        return HiveManager_;
    }

    const ITimestampProviderPtr& GetTimestampProvider() const override
    {
        return TimestampProvider_;
    }

    const ITransactionSupervisorPtr& GetTransactionSupervisor() const override
    {
        return TransactionSupervisor_;
    }

    TMailbox* GetMasterMailbox() const override
    {
        // Create master mailbox lazily.
        auto masterCellId = Bootstrap_->GetCellId();
        return HiveManager_->GetOrCreateMailbox(masterCellId);
    }

    TObjectId GenerateId(EObjectType type) const override
    {
        auto* mutationContext = GetCurrentMutationContext();
        auto version = mutationContext->GetVersion();
        auto random = mutationContext->RandomGenerator()->Generate<ui64>();
        auto cellId = GetCellId();
        return TObjectId(
            random ^ cellId.Parts32[0],
            (cellId.Parts32[1] & 0xffff0000) + static_cast<int>(type),
            version.RecordId,
            version.SegmentId);
    }

    void Initialize() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YT_VERIFY(!Initialized_);

        Initialized_ = true;

        YT_LOG_INFO("Cellar occupant initialized");
    }


    bool CanConfigure() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return Initialized_ && !Finalizing_;
    }

    void Configure(const TConfigureCellSlotInfo& configureInfo) override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);
        YT_VERIFY(CanConfigure());

        auto occupier = GetOccupier();
        auto client = Bootstrap_->GetClient();

        CellDescriptor_ = FromProto<TCellDescriptor>(configureInfo.cell_descriptor());

        // COMPAT(savrus)
        ConfigVersion_ = configureInfo.has_config_version()
            ? configureInfo.config_version()
            : CellDescriptor_.ConfigVersion;

        if (configureInfo.has_peer_id()) {
            TPeerId peerId = configureInfo.peer_id();
            if (PeerId_ != peerId) {
                YT_LOG_DEBUG("Peer id updated (PeerId: %v -> %v)",
                    PeerId_,
                    peerId);

                PeerId_ = peerId;

                // Logger has peer_id tag so should be updated.
                Logger = GetLogger();
            }
        }

        if (configureInfo.has_options()) {
            YT_LOG_DEBUG("Dynamic cell options updated to: %v",
                ConvertToYsonString(TYsonString(configureInfo.options()), EYsonFormat::Text).AsStringBuf());
            Options_ = ConvertTo<TTabletCellOptionsPtr>(TYsonString(configureInfo.options()));
        }

        TDistributedHydraManagerDynamicOptions hydraManagerDynamicOptions;
        hydraManagerDynamicOptions.AbandonLeaderLeaseDuringRecovery = configureInfo.abandon_leader_lease_during_recovery();

        auto newPrerequisiteTransactionId = FromProto<TTransactionId>(configureInfo.prerequisite_transaction_id());
        if (newPrerequisiteTransactionId != PrerequisiteTransactionId_) {
            YT_LOG_INFO("Prerequisite transaction updated (TransactionId: %v -> %v)",
                PrerequisiteTransactionId_,
                newPrerequisiteTransactionId);
            PrerequisiteTransactionId_ = newPrerequisiteTransactionId;
            if (ElectionManager_) {
                ElectionManager_->Abandon(TError("Cell slot reconfigured"));
            }
        }

        PrerequisiteTransaction_.Reset();
        // NB: Prerequisite transaction is only attached by leaders.
        if (PrerequisiteTransactionId_ && CellDescriptor_.Peers[PeerId_].GetVoting()) {
            TTransactionAttachOptions attachOptions;
            attachOptions.Ping = false;
            PrerequisiteTransaction_ = client->AttachTransaction(PrerequisiteTransactionId_, attachOptions);
            YT_LOG_INFO("Prerequisite transaction attached (TransactionId: %v)",
                PrerequisiteTransactionId_);
        }

        // COMPAT(akozhikhov)
        auto connection = Bootstrap_->GetClient()->GetNativeConnection();
        auto snapshotClient = connection->CreateNativeClient(TClientOptions::FromUser(NSecurityClient::TabletCellSnapshotterUserName));
        auto changelogClient = connection->CreateNativeClient(TClientOptions::FromUser(NSecurityClient::TabletCellChangeloggerUserName));

        bool independent = Options_->IndependentPeers;
        TStringBuilder builder;
        builder.AppendFormat("%v/%v", GetCellCypressPrefix(GetCellId()), GetCellId());
        if (independent) {
            builder.AppendFormat("/%v", PeerId_);
        }
        auto path = builder.Flush();

        auto snapshotStore = CreateRemoteSnapshotStore(
            Config_->Snapshots,
            Options_,
            path + "/snapshots",
            snapshotClient,
            PrerequisiteTransaction_ ? PrerequisiteTransaction_->GetId() : NullTransactionId);
        SnapshotStoreThunk_->SetUnderlying(snapshotStore);

        auto addTags = [this] (auto profiler) {
            return profiler
                .WithRequiredTag("tablet_cell_bundle", CellBundleName_ ? CellBundleName_ : UnknownProfilingTag)
                .WithTag("cell_id", ToString(CellDescriptor_.CellId), -1);
        };

        auto changelogProfiler = addTags(occupier->GetProfiler().WithPrefix("/remote_changelog"));
        auto changelogStoreFactory = CreateRemoteChangelogStoreFactory(
            Config_->Changelogs,
            Options_,
            path + "/changelogs",
            changelogClient,
            Bootstrap_->GetResourceLimitsManager(),
            PrerequisiteTransaction_ ? PrerequisiteTransaction_->GetId() : NullTransactionId,
            TJournalWriterPerformanceCounters{changelogProfiler});
        ChangelogStoreFactoryThunk_->SetUnderlying(changelogStoreFactory);

        if (independent) {
            connection->GetCellDirectory()->ReconfigureCell(CellDescriptor_);
        }

        const auto& channelFactory = connection->GetChannelFactory();
        auto alienChannelFactory = CreateAlienCellPeerChannelFactory(connection->GetCellDirectory());

        auto cellConfig = CellDescriptor_.ToConfig(Bootstrap_->GetLocalNetworks());
        CellManager_ = New<TCellManager>(
            cellConfig,
            channelFactory,
            alienChannelFactory,
            PeerId_);

        if (auto slotHydraManager = GetHydraManager()) {
            slotHydraManager->SetDynamicOptions(hydraManagerDynamicOptions);
            ElectionManager_->ReconfigureCell(CellManager_);

            YT_LOG_INFO("Cellar occupant reconfigured (ConfigVersion: %v)",
                CellDescriptor_.ConfigVersion);
        } else {
            Automaton_ = occupier->CreateAutomaton();

            ResponseKeeper_ = CreateResponseKeeper(
                Config_->ResponseKeeper,
                occupier->GetOccupierAutomatonInvoker(),
                Logger,
                Profiler);

            auto rpcServer = Bootstrap_->GetRpcServer();

            TDistributedHydraManagerOptions hydraManagerOptions{
                .UseFork = false,
                .WriteChangelogsAtFollowers = independent,
                .WriteSnapshotsAtFollowers = independent,
                .ResponseKeeper = ResponseKeeper_
            };

            IDistributedHydraManagerPtr hydraManager;
            if (Config_->UseNewHydra) {
                hydraManager = NHydra2::CreateDistributedHydraManager(
                    Config_->HydraManager,
                    Bootstrap_->GetControlInvoker(),
                    occupier->GetMutationAutomatonInvoker(),
                    Automaton_,
                    rpcServer,
                    ElectionManagerThunk_,
                    GetCellId(),
                    ChangelogStoreFactoryThunk_,
                    SnapshotStoreThunk_,
                    Bootstrap_->GetNativeAuthenticator(),
                    hydraManagerOptions,
                    hydraManagerDynamicOptions);
            } else {
                hydraManager = NHydra::CreateDistributedHydraManager(
                    Config_->HydraManager,
                    Bootstrap_->GetControlInvoker(),
                    occupier->GetMutationAutomatonInvoker(),
                    Automaton_,
                    rpcServer,
                    ElectionManagerThunk_,
                    GetCellId(),
                    ChangelogStoreFactoryThunk_,
                    SnapshotStoreThunk_,
                    Bootstrap_->GetNativeAuthenticator(),
                    hydraManagerOptions,
                    hydraManagerDynamicOptions);
            }
            HydraManager_.Store(hydraManager);

            if (!independent) {
                hydraManager->SubscribeLeaderLeaseCheck(
                    BIND(&TCellarOccupant::OnLeaderLeaseCheckThunk, MakeWeak(this))
                        .AsyncVia(Bootstrap_->GetControlInvoker()));
            }

            auto onRecoveryComplete = BIND_NO_PROPAGATE(&TCellarOccupant::OnRecoveryComplete, MakeWeak(this));
            hydraManager->SubscribeControlFollowerRecoveryComplete(onRecoveryComplete);
            hydraManager->SubscribeControlLeaderRecoveryComplete(onRecoveryComplete);

            ElectionManager_ = CreateDistributedElectionManager(
                Config_->ElectionManager,
                CellManager_,
                Bootstrap_->GetControlInvoker(),
                hydraManager->GetElectionCallbacks(),
                rpcServer,
                Bootstrap_->GetNativeAuthenticator());
            ElectionManager_->Initialize();

            ElectionManagerThunk_->SetUnderlying(ElectionManager_);

            HiveManager_ = New<THiveManager>(
                Config_->HiveManager,
                connection->GetCellDirectory(),
                GetCellId(),
                occupier->GetOccupierAutomatonInvoker(),
                hydraManager,
                Automaton_,
                CreateHydraManagerUpstreamSynchronizer(hydraManager),
                Bootstrap_->GetNativeAuthenticator());

            auto clockClusterTag = Options_->ClockClusterTag != InvalidCellTag
                ? Options_->ClockClusterTag
                : connection->GetClusterTag();
            ConfigureTimestampProvider(clockClusterTag);

            occupier->Configure(hydraManager);

            connection->GetClusterDirectorySynchronizer()->Start();

            TransactionSupervisor_ = CreateTransactionSupervisor(
                Config_->TransactionSupervisor,
                occupier->GetOccupierAutomatonInvoker(),
                Bootstrap_->GetTransactionTrackerInvoker(),
                hydraManager,
                Automaton_,
                ResponseKeeper_,
                occupier->GetOccupierTransactionManager(),
                GetCellId(),
                clockClusterTag,
                GetTimestampProvider(),
                {
                    CreateTransactionParticipantProvider(connection),
                    CreateTransactionParticipantProvider(connection->GetClusterDirectory())
                },
                Bootstrap_->GetNativeAuthenticator());

            occupier->Initialize();

            hydraManager->Initialize();

            for (const auto& service : TransactionSupervisor_->GetRpcServices()) {
                rpcServer->RegisterService(service);
            }
            rpcServer->RegisterService(HiveManager_->GetRpcService());

            occupier->RegisterRpcServices();

            OrchidService_ = occupier->PopulateOrchidService(CreateOrchidService())
                ->Via(Bootstrap_->GetControlInvoker());

            YT_LOG_INFO("Cellar occupant configured (ConfigVersion: %v)",
                CellDescriptor_.ConfigVersion);
        }
    }

    TDynamicTabletCellOptionsPtr GetDynamicOptions() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return DynamicOptions_.Load();
    }

    int GetDynamicConfigVersion() const override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return DynamicConfigVersion_;
    }

    void UpdateDynamicConfig(const TUpdateCellSlotInfo& updateInfo) override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        auto updateVersion = updateInfo.dynamic_config_version();

        if (DynamicConfigVersion_ >= updateVersion) {
            YT_LOG_DEBUG("Received outdated dynamic config update (DynamicConfigVersion: %v, UpdateVersion: %v)",
                DynamicConfigVersion_,
                updateVersion);
            return;
        }

        try {
            TDynamicTabletCellOptionsPtr dynamicOptions;

            if (updateInfo.has_dynamic_options()) {
                dynamicOptions = New<TDynamicTabletCellOptions>();
                dynamicOptions->SetUnrecognizedStrategy(EUnrecognizedStrategy::Keep);
                dynamicOptions->Load(ConvertTo<INodePtr>(TYsonString(updateInfo.dynamic_options())));
                auto unrecognized = dynamicOptions->GetLocalUnrecognized();

                if (unrecognized->GetChildCount() > 0) {
                    THROW_ERROR_EXCEPTION("Dynamic options contains unrecognized parameters (Unrecognized: %v)",
                        ConvertToYsonString(unrecognized, EYsonFormat::Text).AsStringBuf());
                }
            }

            DynamicConfigVersion_ = updateInfo.dynamic_config_version();
            DynamicOptions_.Store(std::move(dynamicOptions));

            YT_LOG_DEBUG("Updated dynamic config (DynamicConfigVersion: %v)",
                DynamicConfigVersion_);

        } catch (const std::exception& ex) {
            // TODO(savrus): Write this to cell errors once we have them.
            YT_LOG_ERROR(ex, "Error while updating dynamic config");
        }
    }

    TFuture<void> Finalize() override
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (Finalizing_) {
            return FinalizeResult_;
        }

        YT_LOG_INFO("Finalizing cellar occupant");

        Finalizing_ = true;

        GetOccupier()->Stop();

        FinalizeResult_ = BIND(&TCellarOccupant::DoFinalize, MakeStrong(this))
            .AsyncVia(Bootstrap_->GetControlInvoker())
            .Run();

        return FinalizeResult_;
    }

    const IYPathServicePtr& GetOrchidService() const override
    {
        return OrchidService_;
    }

    const TString& GetCellBundleName() const override
    {
        return CellBundleName_;
    }

    const TTabletCellOptionsPtr& GetOptions() const override
    {
        VERIFY_THREAD_AFFINITY_ANY();

        return Options_;
    }

private:
    const TCellarOccupantConfigPtr Config_;
    const ICellarBootstrapProxyPtr Bootstrap_;
    TAtomicObject<ICellarOccupierPtr> Occupier_;

    const TElectionManagerThunkPtr ElectionManagerThunk_ = New<TElectionManagerThunk>();
    const TSnapshotStoreThunkPtr SnapshotStoreThunk_ = New<TSnapshotStoreThunk>();
    const TChangelogStoreFactoryThunkPtr ChangelogStoreFactoryThunk_ = New<TChangelogStoreFactoryThunk>();

    int Index_;

    TPeerId PeerId_;
    TCellDescriptor CellDescriptor_;
    int ConfigVersion_ = 0;

    const TString CellBundleName_;

    TAtomicObject<TDynamicTabletCellOptionsPtr> DynamicOptions_ = New<TDynamicTabletCellOptions>();
    int DynamicConfigVersion_ = -1;

    TTabletCellOptionsPtr Options_;

    TTransactionId PrerequisiteTransactionId_;
    ITransactionPtr PrerequisiteTransaction_;  // only created for leaders

    TCellManagerPtr CellManager_;

    IElectionManagerPtr ElectionManager_;

    TAtomicObject<IDistributedHydraManagerPtr> HydraManager_;

    IResponseKeeperPtr ResponseKeeper_;

    THiveManagerPtr HiveManager_;

    ITimestampProviderPtr TimestampProvider_;

    ITransactionSupervisorPtr TransactionSupervisor_;

    TCompositeAutomatonPtr Automaton_;

    bool Initialized_ = false;
    bool Finalizing_ = false;
    TFuture<void> FinalizeResult_;

    IYPathServicePtr OrchidService_;

    NLogging::TLogger Logger;

    TCompositeMapServicePtr CreateOrchidService()
    {
        return New<TCompositeMapService>()
            ->AddAttribute(EInternedAttributeKey::Opaque, BIND([] (IYsonConsumer* consumer) {
                    BuildYsonFluently(consumer)
                        .Value(true);
                }))
            ->AddChild("state", IYPathService::FromMethod(
                &TCellarOccupant::GetControlState,
                MakeWeak(this)))
            ->AddChild("hydra", IYPathService::FromMethod(
                &TCellarOccupant::GetHydraMonitoring,
                MakeWeak(this)))
            ->AddChild("config_version", IYPathService::FromMethod(
                &TCellarOccupant::GetConfigVersion,
                MakeWeak(this)))
            ->AddChild("dynamic_options", IYPathService::FromMethod(
                &TCellarOccupant::GetDynamicOptions,
                MakeWeak(this)))
            ->AddChild("dynamic_config_version", IYPathService::FromMethod(
                &TCellarOccupant::GetDynamicConfigVersion,
                MakeWeak(this)))
            ->AddChild("prerequisite_transaction_id", IYPathService::FromMethod(
                &TCellarOccupant::GetPrerequisiteTransactionId,
                MakeWeak(this)))
            ->AddChild("options", IYPathService::FromMethod(
                &TCellarOccupant::GetOptions,
                MakeWeak(this)))
            ->AddChild("hive", HiveManager_->GetOrchidService());
    }

    void GetHydraMonitoring(IYsonConsumer* consumer) const
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (auto hydraManager = GetHydraManager()) {
            hydraManager->GetMonitoringProducer().Run(consumer);
        } else {
            BuildYsonFluently(consumer)
                .Entity();
        }
    }

    TTransactionId GetPrerequisiteTransactionId() const
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        return PrerequisiteTransactionId_;
    }


    static TFuture<void> OnLeaderLeaseCheckThunk(TWeakPtr<TCellarOccupant> weakThis)
    {
        auto this_ = weakThis.Lock();
        return this_ ? this_->OnLeaderLeaseCheck() : VoidFuture;
    }

    TFuture<void> OnLeaderLeaseCheck()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        if (PrerequisiteTransaction_) {
            YT_LOG_DEBUG("Checking prerequisite transaction");
            TTransactionPingOptions options{
                .EnableRetries = true
            };
            return PrerequisiteTransaction_->Ping(options);
        } else {
            return MakeFuture<void>(TError("No prerequisite transaction is attached"));
        }
    }


    void DoFinalize()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        CellManager_.Reset();

        // Stop everything and release the references to break cycles.
        if (auto hydraManager = GetHydraManager()) {
            WaitFor(hydraManager->Finalize())
                .ThrowOnError();
        }
        HydraManager_.Store(nullptr);

        if (ElectionManager_) {
            ElectionManager_->Finalize();
        }
        ElectionManager_.Reset();

        Automaton_.Reset();

        ResponseKeeper_.Reset();

        auto rpcServer = Bootstrap_->GetRpcServer();

        if (TransactionSupervisor_) {
            for (const auto& service : TransactionSupervisor_->GetRpcServices()) {
                rpcServer->UnregisterService(service);
            }
        }
        TransactionSupervisor_.Reset();

        if (HiveManager_) {
            rpcServer->UnregisterService(HiveManager_->GetRpcService());
        }
        HiveManager_.Reset();

        GetOccupier()->Finalize();

        Occupier_.Store(nullptr);
    }

    void OnRecoveryComplete()
    {
        VERIFY_THREAD_AFFINITY(ControlThread);

        // Notify master about recovery completion as soon as possible via out-of-order heartbeat.
        Bootstrap_->ScheduleCellarHeartbeat(/*immediately*/ true);
    }

    void ConfigureTimestampProvider(TCellTag clockClusterTag)
    {
        const auto& connection = Bootstrap_->GetClient()->GetNativeConnection();

        YT_LOG_INFO("Configure cell timestamp provider (ClockClusterTag: %v, ClusterTag: %v)",
            clockClusterTag,
            connection->GetClusterTag());

        TimestampProvider_ = clockClusterTag == connection->GetClusterTag()
            ? connection->GetTimestampProvider()
            : CreateRemoteClusterTimestampProvider(connection, clockClusterTag, Logger);
    }

    NLogging::TLogger GetLogger() const
    {
        return CellarAgentLogger.WithTag("CellId: %v, PeerId: %v",
            CellDescriptor_.CellId,
            PeerId_);
    }


    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);
    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);
};

////////////////////////////////////////////////////////////////////////////////

ICellarOccupantPtr CreateCellarOccupant(
    int index,
    TCellarOccupantConfigPtr config,
    ICellarBootstrapProxyPtr bootstrap,
    const TCreateCellSlotInfo& createInfo,
    ICellarOccupierPtr occupier)
{
    return New<TCellarOccupant>(
        std::move(config),
        std::move(bootstrap),
        index,
        createInfo,
        std::move(occupier));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCellarAgent
