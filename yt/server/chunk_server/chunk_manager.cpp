#include "stdafx.h"
#include "chunk_manager.h"
#include "config.h"
#include "node.h"
#include "chunk.h"
#include "chunk_list.h"
#include "job.h"
#include "job_list.h"
#include "chunk_placement.h"
#include "chunk_replicator.h"
#include "node_lease_tracker.h"
#include "node_statistics.h"
#include "chunk_service_proxy.h"
#include "node_authority.h"
#include "node_statistics.h"
#include "chunk_tree_balancer.h"
#include "chunk_proxy.h"
#include "chunk_list_proxy.h"
#include "node_directory_builder.h"
#include "private.h"

#include <ytlib/misc/foreach.h>
#include <ytlib/misc/serialize.h>
#include <ytlib/misc/guid.h>
#include <ytlib/misc/id_generator.h>
#include <ytlib/misc/address.h>
#include <ytlib/misc/string.h>
#include <ytlib/misc/protobuf_helpers.h>

#include <ytlib/compression/codec.h>

#include <ytlib/erasure_codecs/codec.h>

#include <ytlib/chunk_client/chunk_ypath.pb.h>
#include <ytlib/chunk_client/chunk_list_ypath.pb.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>

#include <ytlib/table_client/table_chunk_meta.pb.h>
#include <ytlib/table_client/table_ypath.pb.h>
#include <ytlib/table_client/schema.h>

#include <ytlib/meta_state/meta_state_manager.h>
#include <ytlib/meta_state/composite_meta_state.h>
#include <ytlib/meta_state/map.h>

#include <ytlib/ytree/fluent.h>

#include <ytlib/logging/log.h>

#include <server/chunk_server/chunk_manager.pb.h>

#include <server/cypress_server/cypress_manager.h>

#include <server/cell_master/serialization_context.h>
#include <server/cell_master/bootstrap.h>
#include <server/cell_master/meta_state_facade.h>

#include <server/transaction_server/transaction_manager.h>
#include <server/transaction_server/transaction.h>

#include <server/object_server/type_handler_detail.h>

#include <server/security_server/security_manager.h>
#include <server/security_server/account.h>
#include <server/security_server/group.h>

namespace NYT {
namespace NChunkServer {

using namespace NRpc;
using namespace NMetaState;
using namespace NTransactionServer;
using namespace NTransactionClient;
using namespace NObjectServer;
using namespace NObjectClient;
using namespace NYTree;
using namespace NCellMaster;
using namespace NCypressServer;
using namespace NSecurityServer;
using namespace NChunkClient;
using namespace NChunkServer::NProto;

using NYT::FromProto;
using NChunkClient::NProto::TReqCreateChunkExt;
using NChunkClient::NProto::TRspCreateChunkExt;

////////////////////////////////////////////////////////////////////////////////

static NLog::TLogger& Logger = ChunkServerLogger;

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TChunkTypeHandlerBase
    : public TObjectTypeHandlerWithMapBase<TChunk>
{
public:
    explicit TChunkTypeHandlerBase(TImpl* owner);

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return TTypeCreationOptions(
            EObjectTransactionMode::Required,
            EObjectAccountMode::Required,
            true);
    }

    virtual TObjectBase* Create(
        TTransaction* transaction,
        TAccount* account,
        IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) override;

protected:
    TImpl* Owner;

    virtual IObjectProxyPtr DoGetProxy(TChunk* chunk, TTransaction* transaction) override;

    virtual void DoDestroy(TChunk* chunk) override;

    virtual void DoUnstage(TChunk* chunk, TTransaction* transaction, bool recursive) override;

};

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TChunkTypeHandler
    : public TChunkTypeHandlerBase
{
public:
    explicit TChunkTypeHandler(TImpl* owner)
        : TChunkTypeHandlerBase(owner)
    { }

    virtual EObjectType GetType() const override
    {
        return EObjectType::Chunk;
    }

private:
    virtual Stroka DoGetName(TChunk* chunk) override
    {
        return Sprintf("chunk %s", ~ToString(chunk->GetId()));
    }

};

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TErasureChunkTypeHandler
    : public TChunkTypeHandlerBase
{
public:
    explicit TErasureChunkTypeHandler(TImpl* owner)
        : TChunkTypeHandlerBase(owner)
    { }

    virtual EObjectType GetType() const override
    {
        return EObjectType::ErasureChunk;
    }

private:
    virtual Stroka DoGetName(TChunk* chunk) override
    {
        return Sprintf("erasure chunk %s", ~ToString(chunk->GetId()));
    }

};

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TChunkListTypeHandler
    : public TObjectTypeHandlerWithMapBase<TChunkList>
{
public:
    explicit TChunkListTypeHandler(TImpl* owner);

    virtual EObjectType GetType() const override
    {
        return EObjectType::ChunkList;
    }

    virtual TNullable<TTypeCreationOptions> GetCreationOptions() const override
    {
        return TTypeCreationOptions(
            EObjectTransactionMode::Required,
            EObjectAccountMode::Forbidden,
            true);
    }

    virtual TObjectBase* Create(
        TTransaction* transaction,
        TAccount* account,
        IAttributeDictionary* attributes,
        TReqCreateObject* request,
        TRspCreateObject* response) override;

private:
    TImpl* Owner;

    virtual Stroka DoGetName(TChunkList* chunkList) override
    {
        return Sprintf("chunk list %s", ~ToString(chunkList->GetId()));
    }

    virtual IObjectProxyPtr DoGetProxy(TChunkList* chunkList, TTransaction* transaction) override;

    virtual void DoDestroy(TChunkList* chunkList) override;

    virtual void DoUnstage(TChunkList* chunkList, TTransaction* transaction, bool recursive) override;

};

////////////////////////////////////////////////////////////////////////////////

class TChunkManager::TImpl
    : public TMetaStatePart
{
public:
    TImpl(
        TChunkManagerConfigPtr config,
        TBootstrap* bootstrap)
        : TMetaStatePart(
            bootstrap->GetMetaStateFacade()->GetManager(),
            bootstrap->GetMetaStateFacade()->GetState())
        , Config(config)
        , Bootstrap(bootstrap)
        , ChunkTreeBalancer(Bootstrap)
        , ChunkReplicaCount(0)
        , RegisteredNodeCount(0)
        , NeedToRecomputeStatistics(false)
        , Profiler(ChunkServerProfiler)
        , AddChunkCounter("/add_chunk_rate")
        , RemoveChunkCounter("/remove_chunk_rate")
        , AddChunkReplicaCounter("/add_chunk_replica_rate")
        , RemoveChunkReplicaCounter("/remove_chunk_replica_rate")
        , StartJobCounter("/start_job_rate")
        , StopJobCounter("/stop_job_rate")
    {
        YCHECK(config);
        YCHECK(bootstrap);

        RegisterMethod(BIND(&TImpl::FullHeartbeat, Unretained(this)));
        RegisterMethod(BIND(&TImpl::IncrementalHeartbeat, Unretained(this)));
        RegisterMethod(BIND(&TImpl::UpdateJobs, Unretained(this)));
        RegisterMethod(BIND(&TImpl::RegisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::UnregisterNode, Unretained(this)));
        RegisterMethod(BIND(&TImpl::UpdateChunkReplicationFactor, Unretained(this)));

        {
            NCellMaster::TLoadContext context;
            context.SetBootstrap(Bootstrap);

            RegisterLoader(
                "ChunkManager.Keys",
                SnapshotVersionValidator(),
                BIND(&TImpl::LoadKeys, MakeStrong(this)),
                context);
            RegisterLoader(
                "ChunkManager.Values",
                SnapshotVersionValidator(),
                BIND(&TImpl::LoadValues, MakeStrong(this)),
                context);
        }

        {
            NCellMaster::TSaveContext context;

            RegisterSaver(
                ESerializationPriority::Keys,
                "ChunkManager.Keys",
                CurrentSnapshotVersion,
                BIND(&TImpl::SaveKeys, MakeStrong(this)),
                context);
            RegisterSaver(
                ESerializationPriority::Values,
                "ChunkManager.Values",
                CurrentSnapshotVersion,
                BIND(&TImpl::SaveValues, MakeStrong(this)),
                context);
        }
    }

    void Initialize()
    {
        auto objectManager = Bootstrap->GetObjectManager();
        objectManager->RegisterHandler(New<TChunkTypeHandler>(this));
        objectManager->RegisterHandler(New<TErasureChunkTypeHandler>(this));
        objectManager->RegisterHandler(New<TChunkListTypeHandler>(this));
    }


    TMutationPtr CreateRegisterNodeMutation(
        const TMetaReqRegisterNode& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::RegisterNode);
    }

    TMutationPtr CreateUnregisterNodeMutation(
        const TMetaReqUnregisterNode& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::UnregisterNode);
    }

    TMutationPtr CreateFullHeartbeatMutation(
        TCtxFullHeartbeatPtr context)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(EStateThreadQueue::Heartbeat)
            ->SetRequestData(context->GetUntypedContext()->GetRequestBody())
            ->SetType(context->Request().GetTypeName())
            ->SetAction(BIND(&TThis::FullHeartbeatWithContext, MakeStrong(this), context));
    }

    TMutationPtr CreateIncrementalHeartbeatMutation(
        const TMetaReqIncrementalHeartbeat& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::IncrementalHeartbeat, EStateThreadQueue::Heartbeat);
    }

    TMutationPtr CreateUpdateJobsMutation(
        const TMetaReqUpdateJobs& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::UpdateJobs);
    }

    TMutationPtr CreateUpdateChunkReplicationFactorMutation(
        const NProto::TMetaReqUpdateChunkReplicationFactor& request)
    {
        return Bootstrap
            ->GetMetaStateFacade()
            ->CreateMutation(this, request, &TThis::UpdateChunkReplicationFactor);
    }


    DECLARE_METAMAP_ACCESSORS(Chunk, TChunk, TChunkId);
    DECLARE_METAMAP_ACCESSORS(ChunkList, TChunkList, TChunkListId);
    DECLARE_METAMAP_ACCESSORS(Node, TDataNode, TNodeId);
    DECLARE_METAMAP_ACCESSORS(JobList, TJobList, TChunkId);
    DECLARE_METAMAP_ACCESSORS(Job, TJob, TJobId);

    DEFINE_SIGNAL(void(const TDataNode*), NodeRegistered);
    DEFINE_SIGNAL(void(const TDataNode*), NodeUnregistered);


    TDataNode* FindNodeByAddresss(const Stroka& address)
    {
        auto it = NodeAddressMap.find(address);
        return it == NodeAddressMap.end() ? nullptr : it->second;
    }

    TDataNode* FindNodeByHostName(const Stroka& hostName)
    {
        auto it = NodeHostNameMap.find(hostName);
        return it == NodeAddressMap.end() ? nullptr : it->second;
    }

    const TReplicationSink* FindReplicationSink(const Stroka& address)
    {
        auto it = ReplicationSinkMap.find(address);
        return it == ReplicationSinkMap.end() ? nullptr : &it->second;
    }

    TSmallVector<TDataNode*, TypicalReplicationFactor> AllocateUploadTargets(
        int replicaCount,
        const TNullable<Stroka>& preferredHostName)
    {
        auto nodes = ChunkPlacement->GetUploadTargets(
            replicaCount,
            nullptr,
            preferredHostName);

        FOREACH (auto* node, nodes) {
            ChunkPlacement->OnSessionHinted(node);
        }

        return nodes;
    }

    TChunk* CreateChunk(EObjectType type)
    {
        Profiler.Increment(AddChunkCounter);
        auto id = Bootstrap->GetObjectManager()->GenerateId(type);
        auto* chunk = new TChunk(id);
        ChunkMap.Insert(id, chunk);
        return chunk;
    }

    TChunkList* CreateChunkList()
    {
        auto id = Bootstrap->GetObjectManager()->GenerateId(EObjectType::ChunkList);
        auto* chunkList = new TChunkList(id);
        ChunkListMap.Insert(id, chunkList);
        return chunkList;
    }


    static TChunkTreeStatistics GetChunkTreeStatistics(TChunkTree* chunkTree)
    {
        switch (chunkTree->GetType()) {
            case EObjectType::Chunk:
                return chunkTree->AsChunk()->GetStatistics();
            case EObjectType::ChunkList:
                return chunkTree->AsChunkList()->Statistics();
            default:
                YUNREACHABLE();
        }
    }

    template <class F>
    static void VisitUniqueAncestors(TChunkList* chunkList, F functor)
    {
        while (chunkList != nullptr) {
            functor(chunkList);
            const auto& parents = chunkList->Parents();
            if (parents.empty())
                break;
            YCHECK(parents.size() == 1);
            chunkList = *parents.begin();
        }
    }

    template <class F>
    static void VisitAncestors(TChunkList* chunkList, F functor)
    {
        // BFS queue. Try to avoid allocations.
        TSmallVector<TChunkList*, 64> queue;
        size_t frontIndex = 0;

        // Put seed into the queue.
        queue.push_back(chunkList);

        // The main loop.
        while (frontIndex < queue.size()) {
            auto* chunkList = queue[frontIndex++];

            // Fast lane: handle unique parents.
            while (chunkList != nullptr) {
                functor(chunkList);
                const auto& parents = chunkList->Parents();
                if (parents.size() != 1)
                    break;
                chunkList = *parents.begin();
            }

            if (chunkList != nullptr) {
                // Proceed to parents.
                FOREACH (auto* parent, chunkList->Parents()) {
                    queue.push_back(parent);
                }
            }
        }
    }


    void AttachToChunkList(
        TChunkList* chunkList,
        TChunkTree** childrenBegin,
        TChunkTree** childrenEnd,
        bool resetSorted)
    {
        auto objectManager = Bootstrap->GetObjectManager();
        chunkList->IncrementVersion();

        TChunkTreeStatistics delta;
        for (auto it = childrenBegin; it != childrenEnd; ++it) {
            auto child = *it;
            if (!chunkList->Children().empty()) {
                chunkList->RowCountSums().push_back(
                    chunkList->Statistics().RowCount +
                    delta.RowCount);
                chunkList->ChunkCountSums().push_back(
                    chunkList->Statistics().ChunkCount +
                    delta.ChunkCount);
            }
            chunkList->Children().push_back(child);
            SetChunkTreeParent(chunkList, child);
            objectManager->RefObject(child);
            delta.Accumulate(GetChunkTreeStatistics(child));
        }

        // Go upwards and apply delta.
        // Reset Sorted flags.
        VisitUniqueAncestors(
            chunkList,
            [&] (TChunkList* current) {
                ++delta.Rank;
                current->Statistics().Accumulate(delta);
                if (resetSorted) {
                    current->SortedBy().clear();
                }
            });
    }

    void AttachToChunkList(
        TChunkList* chunkList,
        const std::vector<TChunkTree*>& children,
        bool resetSorted)
    {
        AttachToChunkList(
            chunkList,
            const_cast<TChunkTree**>(children.data()),
            const_cast<TChunkTree**>(children.data() + children.size()),
            resetSorted);
    }

    void AttachToChunkList(
        TChunkList* chunkList,
        TChunkTree* child,
        bool resetSorted)
    {
        AttachToChunkList(
            chunkList,
            &child,
            &child + 1,
            resetSorted);
    }


    void RebalanceChunkTree(TChunkList* chunkList)
    {
        if (!ChunkTreeBalancer.IsRebalanceNeeded(chunkList))
            return;

        PROFILE_TIMING ("/chunk_tree_rebalance_time") {
            LOG_DEBUG_UNLESS(IsRecovery(), "Chunk tree rebalancing started (RootId: %s)",
                ~ToString(chunkList->GetId()));
            ChunkTreeBalancer.Rebalance(chunkList);
            LOG_DEBUG_UNLESS(IsRecovery(), "Chunk tree rebalancing completed");
        }
    }


    void ConfirmChunk(
        TChunk* chunk,
        const std::vector<NChunkClient::TChunkReplica>& replicas,
        NChunkClient::NProto::TChunkInfo* chunkInfo,
        NChunkClient::NProto::TChunkMeta* chunkMeta)
    {
        YCHECK(!chunk->IsConfirmed());

        auto id = chunk->GetId();

        chunk->ChunkInfo().Swap(chunkInfo);
        chunk->ChunkMeta().Swap(chunkMeta);

        FOREACH (auto clientReplica, replicas) {
            auto* node = FindNode(clientReplica.GetNodeId());
            if (!node) {
                LOG_DEBUG_UNLESS(IsRecovery(), "Tried to confirm chunk %s at an unknown node %d",
                    ~ToString(id),
                    ~clientReplica.GetNodeId());
                continue;
            }

            TDataNodePtrWithIndex nodeWithIndex(node, clientReplica.GetIndex());
            TChunkPtrWithIndex chunkWithIndex(chunk, clientReplica.GetIndex());

            if (node->GetState() != ENodeState::Online) {
                LOG_DEBUG_UNLESS(IsRecovery(), "Tried to confirm chunk %s at %s which has invalid state %s",
                    ~ToString(id),
                    ~node->GetAddress(),
                    ~FormatEnum(node->GetState()).Quote());
                continue;
            }

            if (!node->HasReplica(chunkWithIndex, false)) {
                AddChunkReplica(
                    node,
                    chunkWithIndex,
                    false,
                    EAddReplicaReason::Confirmation);
                node->MarkReplicaUnapproved(chunkWithIndex);
            }
        }

        // Increase staged resource usage.
        if (chunk->IsStaged()) {
            auto* stagingTransaction = chunk->GetStagingTransaction();
            auto* stagingAccount = chunk->GetStagingAccount();
            auto securityManager = Bootstrap->GetSecurityManager();
            auto delta = chunk->GetResourceUsage();
            securityManager->UpdateAccountStagingUsage(stagingTransaction, stagingAccount, delta);
        }

        if (IsLeader()) {
            ChunkReplicator->ScheduleChunkRefresh(chunk);
        }

        LOG_DEBUG_UNLESS(IsRecovery(), "Chunk confirmed (ChunkId: %s)", ~ToString(id));
    }


    void ClearChunkList(TChunkList* chunkList)
    {
        // TODO(babenko): currently we only support clearing a chunklist with no parents.
        YCHECK(chunkList->Parents().empty());
        chunkList->IncrementVersion();

        auto objectManager = Bootstrap->GetObjectManager();
        FOREACH (auto* child, chunkList->Children()) {
            ResetChunkTreeParent(chunkList, child);
            objectManager->UnrefObject(child);
        }
        chunkList->Children().clear();
        chunkList->RowCountSums().clear();
        chunkList->ChunkCountSums().clear();
        chunkList->Statistics() = TChunkTreeStatistics();
        chunkList->Statistics().ChunkListCount = 1;

        LOG_DEBUG_UNLESS(IsRecovery(), "Chunk list cleared (ChunkListId: %s)", ~ToString(chunkList->GetId()));
    }

    void SetChunkTreeParent(TChunkList* parent, TChunkTree* child)
    {
        switch (child->GetType()) {
            case EObjectType::Chunk:
                child->AsChunk()->Parents().push_back(parent);
                break;
            case EObjectType::ChunkList:
                child->AsChunkList()->Parents().insert(parent);
                break;
            default:
                YUNREACHABLE();
        }
    }

    void ResetChunkTreeParent(TChunkList* parent, TChunkTree* child)
    {
        switch (child->GetType()) {
            case EObjectType::Chunk: {
                auto& parents = child->AsChunk()->Parents();
                auto it = std::find(parents.begin(), parents.end(), parent);
                YASSERT(it != parents.end());
                parents.erase(it);
                break;
            }
            case EObjectType::ChunkList: {
                auto& parents = child->AsChunkList()->Parents();
                auto it = parents.find(parent);
                YASSERT(it != parents.end());
                parents.erase(it);
                break;
            }
            default:
                YUNREACHABLE();
        }
    }


    void ScheduleJobs(
        TDataNode* node,
        const std::vector<TJobInfo>& runningJobs,
        std::vector<TJobStartInfo>* jobsToStart,
        std::vector<TJobStopInfo>* jobsToStop)
    {
        ChunkReplicator->ScheduleJobs(
            node,
            runningJobs,
            jobsToStart,
            jobsToStop);
    }

    const yhash_set<TChunk*>& LostChunks() const;
    const yhash_set<TChunk*>& LostVitalChunks() const;
    const yhash_set<TChunk*>& OverreplicatedChunks() const;
    const yhash_set<TChunk*>& UnderreplicatedChunks() const;

    TDataNodePtrWithIndexList GetChunkReplicas(const TChunk* chunk)
    {
        TDataNodePtrWithIndexList result;

        FOREACH (auto replica, chunk->StoredReplicas()) {
            result.push_back(replica);
        }

        if (~chunk->CachedReplicas()) {
            FOREACH (auto replica, *chunk->CachedReplicas()) {
                result.push_back(replica);
            }
        }

        return result;
    }

    TTotalNodeStatistics GetTotalNodeStatistics()
    {
        TTotalNodeStatistics result;
        FOREACH (const auto& pair, NodeMap) {
            const auto* node = pair.second;
            const auto& statistics = node->Statistics();
            result.AvailbaleSpace += statistics.total_available_space();
            result.UsedSpace += statistics.total_used_space();
            result.ChunkCount += statistics.total_chunk_count();
            result.SessionCount += statistics.total_session_count();
            result.OnlineNodeCount++;
        }
        return result;
    }

    bool IsNodeConfirmed(const TDataNode* node)
    {
        return NodeLeaseTracker->IsNodeConfirmed(node);
    }


    int GetChunkReplicaCount()
    {
        return ChunkReplicaCount;
    }

    int GetRegisteredNodeCount()
    {
        return RegisteredNodeCount;
    }

    bool IsReplicatorEnabled()
    {
        return ChunkReplicator->IsEnabled();
    }

    void ScheduleRFUpdate(TChunkTree* chunkTree)
    {
        ChunkReplicator->ScheduleRFUpdate(chunkTree);
    }


    TChunkTree* FindChunkTree(const TChunkTreeId& id)
    {
        auto type = TypeFromId(id);
        switch (type) {
            case EObjectType::Chunk:
                return FindChunk(id);
            case EObjectType::ChunkList:
                return FindChunkList(id);
            default:
                return nullptr;
        }
    }

    TChunkTree* GetChunkTree(const TChunkTreeId& id)
    {
        auto* chunkTree = FindChunkTree(id);
        YCHECK(chunkTree);
        return chunkTree;
    }


    std::vector<TYPath> GetOwningNodes(TChunkTree* chunkTree)
    {
        auto cypressManager = Bootstrap->GetCypressManager();

        yhash_set<TCypressNodeBase*> owningNodes;
        yhash_set<TChunkTree*> visited;
        GetOwningNodes(chunkTree, visited, &owningNodes);

        std::vector<TYPath> paths;
        FOREACH (auto* node, owningNodes) {
            auto* trunkNode = node->GetTrunkNode();
            auto proxy = cypressManager->GetVersionedNodeProxy(trunkNode);
            auto path = proxy->GetPath();
            paths.push_back(path);
        }

        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        return paths;
    }

private:
    typedef TImpl TThis;
    friend class TChunkTypeHandlerBase;
    friend class TChunkTypeHandler;
    friend class TErasureChunkTypeHandler;
    friend class TChunkListTypeHandler;

    TChunkManagerConfigPtr Config;
    TBootstrap* Bootstrap;

    TChunkTreeBalancer ChunkTreeBalancer;

    int ChunkReplicaCount;
    int RegisteredNodeCount;

    bool NeedToRecomputeStatistics;

    NProfiling::TProfiler& Profiler;
    NProfiling::TRateCounter AddChunkCounter;
    NProfiling::TRateCounter RemoveChunkCounter;
    NProfiling::TRateCounter AddChunkReplicaCounter;
    NProfiling::TRateCounter RemoveChunkReplicaCounter;
    NProfiling::TRateCounter StartJobCounter;
    NProfiling::TRateCounter StopJobCounter;

    TChunkPlacementPtr ChunkPlacement;
    TChunkReplicatorPtr ChunkReplicator;
    TNodeLeaseTrackerPtr NodeLeaseTracker;

    TIdGenerator NodeIdGenerator;

    TMetaStateMap<TChunkId, TChunk> ChunkMap;
    TMetaStateMap<TChunkListId, TChunkList> ChunkListMap;

    TMetaStateMap<TNodeId, TDataNode> NodeMap;
    yhash_map<Stroka, TDataNode*> NodeAddressMap;
    yhash_multimap<Stroka, TDataNode*> NodeHostNameMap;

    TMetaStateMap<TChunkId, TJobList> JobListMap;
    TMetaStateMap<TJobId, TJob> JobMap;

    yhash_map<Stroka, TReplicationSink> ReplicationSinkMap;


    void DestroyChunk(TChunk* chunk)
    {
        auto chunkId = chunk->GetId();

        // Decrease staging resource usage.
        if (chunk->IsStaged()) {
            UnstageChunk(chunk);
        }

        auto scheduleRemoval = [&] (TDataNodePtrWithIndex nodeWithIndex, bool cached) {
            ScheduleChunkReplicaRemoval(
                nodeWithIndex.GetPtr(),
                TChunkPtrWithIndex(chunk, nodeWithIndex.GetIndex()),
                cached);
        };

        // Unregister chunk replicas from all known locations.
        FOREACH (auto replica, chunk->StoredReplicas()) {
            scheduleRemoval(replica, false);
        }
        if (~chunk->CachedReplicas()) {
            FOREACH (auto replica, *chunk->CachedReplicas()) {
                scheduleRemoval(replica, true);
            }
        }

        // Remove all associated jobs.
        auto* jobList = FindJobList(chunkId);
        if (jobList) {
            FOREACH (auto job, jobList->Jobs()) {
                // Suppress removal from job list.
                RemoveJob(job, true, false);
            }
            JobListMap.Remove(chunkId);
        }

        // Notify the replicator about chunk's death.
        if (ChunkReplicator) {
            ChunkReplicator->OnChunkRemoved(chunk);
        }

        Profiler.Increment(RemoveChunkCounter);
    }

    void DestroyChunkList(TChunkList* chunkList)
    {
        auto objectManager = Bootstrap->GetObjectManager();
        // Drop references to children.
        FOREACH (auto* child, chunkList->Children()) {
            ResetChunkTreeParent(chunkList, child);
            objectManager->UnrefObject(child);
        }
    }


    void UnstageChunk(TChunk* chunk)
    {
        if (chunk->IsStaged() && chunk->IsConfirmed()) {
            auto* stagingTransaction = chunk->GetStagingTransaction();
            auto* stagingAccount = chunk->GetStagingAccount();
            auto securityManager = Bootstrap->GetSecurityManager();
            auto delta = -chunk->GetResourceUsage();
            securityManager->UpdateAccountStagingUsage(stagingTransaction, stagingAccount, delta);
        }

        chunk->SetStagingTransaction(nullptr);
        chunk->SetStagingAccount(nullptr);
    }

    void UnstageChunkList(
        TChunkList* chunkList,
        TTransaction* transaction,
        bool recursive)
    {
        if (!recursive)
            return;

        auto objectManager = Bootstrap->GetObjectManager();
        FOREACH (auto* child, chunkList->Children()) {
            objectManager->UnstageObject(transaction, child, true);
        }
    }


    TNodeId GenerateNodeId()
    {
        TNodeId id;
        while (true) {
            id = NodeIdGenerator.Next();
            // Beware of sentinels!
            if (id == InvalidNodeId) {
                // Just wait for the next attempt.
            } else if (id > MaxNodeId) {
                NodeIdGenerator.Reset();
            } else {
                break;
            }
        }
        return id;
    }

    TMetaRspRegisterNode RegisterNode(const TMetaReqRegisterNode& request)
    {
        auto descriptor = FromProto<NChunkClient::TNodeDescriptor>(request.node_descriptor());
        const auto& statistics = request.statistics();
        const auto& address = descriptor.Address;

        auto nodeId = GenerateNodeId();

        auto* existingNode = FindNodeByAddresss(descriptor.Address);
        if (existingNode) {
            LOG_INFO_UNLESS(IsRecovery(), "Node kicked out due to address conflict (Address: %s, ExistingId: %d)",
                ~address,
                existingNode->GetId());
            DoUnregisterNode(existingNode);
        }

        LOG_INFO_UNLESS(IsRecovery(), "Node registered (NodeId: %d, Address: %s, %s)",
            nodeId,
            ~address,
            ~ToString(statistics));

        auto* newNode = new TDataNode(nodeId, descriptor);
        newNode->SetState(ENodeState::Registered);
        newNode->Statistics() = statistics;

        NodeMap.Insert(nodeId, newNode);
        NodeAddressMap.insert(std::make_pair(address, newNode));
        NodeHostNameMap.insert(std::make_pair(Stroka(GetServiceHostName(address)), newNode));
        ++RegisteredNodeCount;

        if (IsLeader()) {
            ChunkPlacement->OnNodeRegistered(newNode);
            ChunkReplicator->OnNodeRegistered(newNode);
            StartNodeTracking(newNode, false);
        }

        TMetaRspRegisterNode response;
        response.set_node_id(nodeId);
        return response;
    }

    void UnregisterNode(const TMetaReqUnregisterNode& request)
    {
        auto nodeId = request.node_id();

        // Allow nodeId to be invalid, just ignore such obsolete requests.
        auto* node = FindNode(nodeId);
        if (node) {
            DoUnregisterNode(node);
        }
    }


    void FullHeartbeatWithContext(TCtxFullHeartbeatPtr context)
    {
        return FullHeartbeat(context->Request());
    }

    void FullHeartbeat(const TMetaReqFullHeartbeat& request)
    {
        PROFILE_TIMING ("/full_heartbeat_time") {
            Profiler.Enqueue("/full_heartbeat_chunks", request.chunks_size());

            auto nodeId = request.node_id();
            const auto& statistics = request.statistics();

            auto* node = GetNode(nodeId);

            LOG_DEBUG_UNLESS(IsRecovery(), "Full heartbeat received (NodeId: %d, Address: %s, State: %s, %s, Chunks: %d)",
                nodeId,
                ~node->GetAddress(),
                ~node->GetState().ToString(),
                ~ToString(statistics),
                static_cast<int>(request.chunks_size()));

            YCHECK(node->GetState() == ENodeState::Registered);
            node->SetState(ENodeState::Online);
            --RegisteredNodeCount;

            node->Statistics() = statistics;

            if (IsLeader()) {
                NodeLeaseTracker->OnNodeOnline(node, false);
                ChunkPlacement->OnNodeUpdated(node);
            }

            LOG_INFO_UNLESS(IsRecovery(), "Node online (NodeId: %d, Address: %s)",
                nodeId,
                ~node->GetAddress());

            YCHECK(node->StoredReplicas().empty());
            YCHECK(node->CachedReplicas().empty());

            FOREACH (const auto& chunkInfo, request.chunks()) {
                ProcessAddedChunk(node, chunkInfo, false);
            }
        }
    }

    void IncrementalHeartbeat(const TMetaReqIncrementalHeartbeat& request)
    {
        Profiler.Enqueue("/incremental_heartbeat_chunks_added", request.added_chunks_size());
        Profiler.Enqueue("/incremental_heartbeat_chunks_removed", request.removed_chunks_size());
        PROFILE_TIMING ("/incremental_heartbeat_time") {
            auto nodeId = request.node_id();
            const auto& statistics = request.statistics();

            auto* node = GetNode(nodeId);

            LOG_DEBUG_UNLESS(IsRecovery(), "Incremental heartbeat received (NodeId: %d, Address: %s, State: %s, %s, ChunksAdded: %d, ChunksRemoved: %d)",
                nodeId,
                ~node->GetAddress(),
                ~node->GetState().ToString(),
                ~ToString(statistics),
                static_cast<int>(request.added_chunks_size()),
                static_cast<int>(request.removed_chunks_size()));

            YCHECK(node->GetState() == ENodeState::Online);
            node->Statistics() = statistics;

            if (IsLeader()) {
                NodeLeaseTracker->OnNodeHeartbeat(node);
                ChunkPlacement->OnNodeUpdated(node);
            }

            FOREACH (const auto& chunkInfo, request.added_chunks()) {
                ProcessAddedChunk(node, chunkInfo, true);
            }

            FOREACH (const auto& chunkInfo, request.removed_chunks()) {
                ProcessRemovedChunk(node, chunkInfo);
            }

            std::vector<TChunkPtrWithIndex> unapprovedReplicas(node->UnapprovedReplicas().begin(), node->UnapprovedReplicas().end());
            FOREACH (auto replica, unapprovedReplicas) {
                RemoveChunkReplica(node, replica, false, ERemoveReplicaReason::Unapproved);
            }
            node->UnapprovedReplicas().clear();
        }
    }


    void UpdateJobs(const TMetaReqUpdateJobs& request)
    {
        PROFILE_TIMING ("/update_jobs_time") {
            auto nodeId = request.node_id();
            auto* node = GetNode(nodeId);

            FOREACH (const auto& startInfo, request.started_jobs()) {
                AddJob(node, startInfo);
            }

            FOREACH (const auto& stopInfo, request.stopped_jobs()) {
                auto jobId = FromProto<TJobId>(stopInfo.job_id());
                auto* job = FindJob(jobId);
                if (job) {
                    // Remove from both job list and node.
                    RemoveJob(job, true, true);
                }
            }

            LOG_DEBUG_UNLESS(IsRecovery(), "Node jobs updated (NodeId: %d, Address: %s, JobsStarted: %d, JobsStopped: %d)",
                nodeId,
                ~node->GetAddress(),
                static_cast<int>(request.started_jobs_size()),
                static_cast<int>(request.stopped_jobs_size()));
        }
    }


    void UpdateChunkReplicationFactor(const TMetaReqUpdateChunkReplicationFactor& request)
    {
        FOREACH (const auto& update, request.updates()) {
            auto chunkId = FromProto<TChunkId>(update.chunk_id());
            int replicationFactor = update.replication_factor();
            auto* chunk = FindChunk(chunkId);
            if (IsObjectAlive(chunk) && chunk->GetReplicationFactor() != replicationFactor) {
                // NB: Updating RF for staged chunks is forbidden.
                YCHECK(!chunk->IsStaged());
                chunk->SetReplicationFactor(replicationFactor);
                if (IsLeader()) {
                    ChunkReplicator->ScheduleChunkRefresh(chunk);
                }
            }
        }
    }


    void SaveKeys(const NCellMaster::TSaveContext& context) const
    {
        ChunkMap.SaveKeys(context);
        ChunkListMap.SaveKeys(context);
        NodeMap.SaveKeys(context);
        JobMap.SaveKeys(context);
        JobListMap.SaveKeys(context);
    }

    void SaveValues(const NCellMaster::TSaveContext& context) const
    {
        Save(context, NodeIdGenerator);
        ChunkMap.SaveValues(context);
        ChunkListMap.SaveValues(context);
        NodeMap.SaveValues(context);
        JobMap.SaveValues(context);
        JobListMap.SaveValues(context);
    }

    void LoadKeys(const NCellMaster::TLoadContext& context)
    {
        ChunkMap.LoadKeys(context);
        ChunkListMap.LoadKeys(context);
        NodeMap.LoadKeys(context);
        JobMap.LoadKeys(context);
        JobListMap.LoadKeys(context);
    }

    void LoadValues(const NCellMaster::TLoadContext& context)
    {
        Load(context, NodeIdGenerator);
        ChunkMap.LoadValues(context);
        ChunkListMap.LoadValues(context);
        // COMPAT(ignat)
        if (context.GetVersion() < 8) {
            ScheduleRecomputeStatistics();
        }

        NodeMap.LoadValues(context);
        JobMap.LoadValues(context);
        JobListMap.LoadValues(context);

        // Compute chunk replica count.
        ChunkReplicaCount = 0;
        FOREACH (const auto& pair, NodeMap) {
            const auto* node = pair.second;
            ChunkReplicaCount += node->StoredReplicas().size();
            ChunkReplicaCount += node->CachedReplicas().size();
        }

        // Reconstruct address maps.
        NodeAddressMap.clear();
        NodeHostNameMap.clear();
        RegisteredNodeCount = 0;
        FOREACH (const auto& pair, NodeMap) {
            auto* node = pair.second;
            const auto& address = node->GetAddress();
            YCHECK(NodeAddressMap.insert(std::make_pair(address, node)).second);
            NodeHostNameMap.insert(std::make_pair(Stroka(GetServiceHostName(address)), node));
            if (node->GetState() == ENodeState::Registered) {
                ++RegisteredNodeCount;
            }
        }

        // Reconstruct ReplicationSinkMap.
        ReplicationSinkMap.clear();
        FOREACH (auto& pair, JobMap) {
            RegisterReplicationSinks(pair.second);
        }
    }

    virtual void Clear() override
    {
        NodeIdGenerator.Reset();
        // XXX(babenko): avoid generating InvalidNodeId
        NodeIdGenerator.Next();
        ChunkMap.Clear();
        ChunkListMap.Clear();
        NodeMap.Clear();
        JobMap.Clear();
        JobListMap.Clear();

        NodeAddressMap.clear();
        NodeHostNameMap.clear();

        ReplicationSinkMap.clear();

        ChunkReplicaCount = 0;
        RegisteredNodeCount = 0;
    }


    void ScheduleRecomputeStatistics()
    {
        NeedToRecomputeStatistics = true;
    }

    const TChunkTreeStatistics& ComputeStatisticsFor(TChunkList* chunkList)
    {
        auto& statistics = chunkList->Statistics();
        if (statistics.Rank == -1) {
            statistics = TChunkTreeStatistics();
            FOREACH (auto* child, chunkList->Children()) {
                switch (child->GetType()) {
                    case EObjectType::Chunk:
                        statistics.Accumulate(child->AsChunk()->GetStatistics());
                        break;

                    case EObjectType::ChunkList:
                        statistics.Accumulate(ComputeStatisticsFor(child->AsChunkList()));
                        break;

                    default:
                        YUNREACHABLE();
                }
            }
            if (!chunkList->Children().empty()) {
                ++statistics.Rank;
            }
            ++statistics.ChunkListCount;
        }
        return statistics;
    }

    void RecomputeStatistics()
    {
        // Chunk trees traversal with memoization.

        LOG_INFO("Started recomputing statistics");

        // Use Rank field for keeping track of already visited chunk lists.
        FOREACH (auto& pair, ChunkListMap) {
            auto* chunkList = pair.second;
            chunkList->Statistics().Rank = -1;
        }

        // Force all statistics to be recalculated.
        FOREACH (auto& pair, ChunkListMap) {
            auto* chunkList = pair.second;
            ComputeStatisticsFor(chunkList);
        }

        LOG_INFO("Finished recomputing statistics");
    }


    virtual void OnRecoveryStarted() override
    {
        Profiler.SetEnabled(false);

        NeedToRecomputeStatistics = false;
    }

    virtual void OnRecoveryComplete() override
    {
        Profiler.SetEnabled(true);

        if (NeedToRecomputeStatistics) {
            RecomputeStatistics();
            NeedToRecomputeStatistics = false;
        }

        // Reset runtime info.
        FOREACH (const auto& pair, ChunkMap) {
            auto* chunk = pair.second;
            chunk->SetRefreshScheduled(false);
            chunk->SetRFUpdateScheduled(false);
            chunk->ResetObjectLocks();
        }

        FOREACH (const auto& pair, ChunkListMap) {
            auto* chunkList = pair.second;
            chunkList->ResetObjectLocks();
        }
    }

    virtual void OnLeaderRecoveryComplete() override
    {
        NodeLeaseTracker = New<TNodeLeaseTracker>(Config, Bootstrap);
        ChunkPlacement = New<TChunkPlacement>(Config, Bootstrap);
        ChunkReplicator = New<TChunkReplicator>(Config, Bootstrap, ChunkPlacement, NodeLeaseTracker);

        LOG_INFO("Full chunk refresh started");
        PROFILE_TIMING ("/full_chunk_refresh_time") {
            FOREACH (auto& pair, NodeMap) {
                auto* node = pair.second;
                ChunkPlacement->OnNodeRegistered(node);
                ChunkReplicator->OnNodeRegistered(node);
            }
            ChunkReplicator->Start();
        }
        LOG_INFO("Full chunk refresh completed");
    }

    virtual void OnActiveQuorumEstablished() override
    {
        // Assign initial leases to nodes.
        // NB: Nodes will remain unconfirmed until the first heartbeat.
        FOREACH (const auto& pair, NodeMap) {
            StartNodeTracking(pair.second, true);
        }
    }

    virtual void OnStopLeading() override
    {
        ChunkPlacement.Reset();
        ChunkReplicator.Reset();
        NodeLeaseTracker.Reset();
    }


    void StartNodeTracking(TDataNode* node, bool recovery)
    {
        NodeLeaseTracker->OnNodeRegistered(node, recovery);
        if (node->GetState() == ENodeState::Online) {
            NodeLeaseTracker->OnNodeOnline(node, recovery);
        }
        NodeRegistered_.Fire(node);
    }

    void StopNodeTracking(TDataNode* node)
    {
        NodeLeaseTracker->OnNodeUnregistered(node);
        NodeUnregistered_.Fire(node);
    }


    void DoUnregisterNode(TDataNode* node)
    {
        PROFILE_TIMING ("/node_unregistration_time") {
            auto nodeId = node->GetId();

            LOG_INFO_UNLESS(IsRecovery(), "Node unregistered (NodeId: %d, Address: %s)",
                nodeId,
                ~node->GetAddress());

            if (IsLeader()) {
                ChunkPlacement->OnNodeUnregistered(node);
                ChunkReplicator->OnNodeUnregistered(node);
                StopNodeTracking(node);
            }

            FOREACH (auto replica, node->StoredReplicas()) {
                RemoveChunkReplica(node, replica, false, ERemoveReplicaReason::Reset);
            }

            FOREACH (auto replica, node->CachedReplicas()) {
                RemoveChunkReplica(node, replica, true, ERemoveReplicaReason::Reset);
            }

            FOREACH (auto* job, node->Jobs()) {
                // Suppress removal of job from node.
                RemoveJob(job, false, true);
            }

            const auto& address = node->GetAddress();
            YCHECK(NodeAddressMap.erase(address) == 1);
            {
                auto hostNameRange = NodeHostNameMap.equal_range(Stroka(GetServiceHostName(address)));
                for (auto it = hostNameRange.first; it != hostNameRange.second; ++it) {
                    if (it->second == node) {
                        NodeHostNameMap.erase(it);
                        break;
                    }
                }
            }

            if (node->GetState() == ENodeState::Registered) {
                --RegisteredNodeCount;
            }

            NodeMap.Remove(nodeId);
        }
    }


    DECLARE_ENUM(EAddReplicaReason,
        (IncrementalHeartbeat)
        (FullHeartbeat)
        (Confirmation)
    );

    void AddChunkReplica(TDataNode* node, TChunkPtrWithIndex chunkWithIndex, bool cached, EAddReplicaReason reason)
    {
        auto* chunk = chunkWithIndex.GetPtr();
        auto nodeId = node->GetId();
        TDataNodePtrWithIndex nodeWithIndex(node, chunkWithIndex.GetIndex());

        if (node->HasReplica(chunkWithIndex, cached)) {
            LOG_DEBUG_UNLESS(IsRecovery(), "Chunk replica is already added (ChunkId: %s, Cached: %s, Reason: %s, NodeId: %d, Address: %s)",
                ~ToString(chunkWithIndex),
                ~FormatBool(cached),
                ~reason.ToString(),
                nodeId,
                ~node->GetAddress());
            return;
        }

        node->AddReplica(chunkWithIndex, cached);
        chunk->AddReplica(nodeWithIndex, cached);

        if (!IsRecovery()) {
            LOG_EVENT(
                Logger,
                reason == EAddReplicaReason::FullHeartbeat ? NLog::ELogLevel::Trace : NLog::ELogLevel::Debug,
                "Chunk replica added (ChunkId: %s, Cached: %s, NodeId: %d, Address: %s)",
                ~ToString(chunkWithIndex),
                ~FormatBool(cached),
                nodeId,
                ~node->GetAddress());
        }

        if (!cached && IsLeader()) {
            ChunkReplicator->ScheduleChunkRefresh(chunkWithIndex.GetPtr());
        }

        if (reason == EAddReplicaReason::IncrementalHeartbeat || reason == EAddReplicaReason::Confirmation) {
            Profiler.Increment(AddChunkReplicaCounter);
        }
    }

    void ScheduleChunkReplicaRemoval(TDataNode* node, TChunkPtrWithIndex chunkWithIndex, bool cached)
    {
        node->RemoveReplica(chunkWithIndex, cached);
        if (!cached && IsLeader()) {
            ChunkReplicator->ScheduleChunkRemoval(node, chunkWithIndex);
        }
    }

    DECLARE_ENUM(ERemoveReplicaReason,
        (IncrementalHeartbeat)
        (Unapproved)
        (Reset)
    );

    void RemoveChunkReplica(TDataNode* node, TChunkPtrWithIndex chunkWithIndex, bool cached, ERemoveReplicaReason reason)
    {
        auto* chunk = chunkWithIndex.GetPtr();
        auto nodeId = node->GetId();
        TDataNodePtrWithIndex nodeWithIndex(node, chunkWithIndex.GetIndex());

        if (reason == ERemoveReplicaReason::IncrementalHeartbeat && !node->HasReplica(chunkWithIndex, cached)) {
            LOG_DEBUG_UNLESS(IsRecovery(), "Chunk replica is already removed (ChunkId: %s, Cached: %s, Reason: %s, NodeId: %d, Address: %s)",
                ~ToString(chunkWithIndex),
                ~FormatBool(cached),
                ~reason.ToString(),
                nodeId,
                ~node->GetAddress());
            return;
        }

        switch (reason) {
            case ERemoveReplicaReason::IncrementalHeartbeat:
            case ERemoveReplicaReason::Unapproved:
                node->RemoveReplica(chunkWithIndex, cached);
                break;
            case ERemoveReplicaReason::Reset:
                // Do nothing.
                break;
            default:
                YUNREACHABLE();
        }
        chunk->RemoveReplica(nodeWithIndex, cached);

        if (!IsRecovery()) {
            LOG_EVENT(
                Logger,
                reason == ERemoveReplicaReason::Reset ? NLog::ELogLevel::Trace : NLog::ELogLevel::Debug,
                "Chunk replica removed (ChunkId: %s, Cached: %s, Reason: %s, NodeId: %d, Address: %s)",
                ~ToString(chunkWithIndex),
                ~FormatBool(cached),
                ~reason.ToString(),
                nodeId,
                ~node->GetAddress());
        }

        if (!cached && IsLeader()) {
            ChunkReplicator->ScheduleChunkRefresh(chunk);
        }

        Profiler.Increment(RemoveChunkReplicaCounter);
    }


    void AddJob(TDataNode* node, const TJobStartInfo& jobInfo)
    {
        auto* mutationContext = Bootstrap
            ->GetMetaStateFacade()
            ->GetManager()
            ->GetMutationContext();

        auto nodeId = node->GetId();
        auto chunkId = FromProto<TChunkId>(jobInfo.chunk_id());
        auto jobId = FromProto<TJobId>(jobInfo.job_id());
        auto jobType = EJobType(jobInfo.type());
        auto targets = FromProto<TNodeDescriptor>(jobInfo.targets());

        std::vector<Stroka> targetAddresses;
        FOREACH (const auto& target, targets) {
            targetAddresses.push_back(target.Address);
        }

        auto* job = new TJob(
            jobType,
            jobId,
            chunkId,
            node->GetAddress(),
            targetAddresses,
            mutationContext->GetTimestamp());
        JobMap.Insert(jobId, job);

        auto* jobList = GetOrCreateJobList(chunkId);
        jobList->AddJob(job);

        node->AddJob(job);

        RegisterReplicationSinks(job);

        LOG_INFO_UNLESS(IsRecovery(), "Job added (JobId: %s, NodeId: %d, Address: %s, JobType: %s, ChunkId: %s)",
            ~ToString(jobId),
            nodeId,
            ~node->GetAddress(),
            ~jobType.ToString(),
            ~ToString(chunkId));
    }

    void RemoveJob(
        TJob* job,
        bool removeFromNode,
        bool removeFromJobList)
    {
        auto chunkId = job->GetChunkId();
        auto jobId = job->GetId();

        if (removeFromJobList) {
            auto* jobList = GetJobList(chunkId);
            jobList->RemoveJob(job);
            DropJobListIfEmpty(jobList);
        }

        if (removeFromNode) {
            auto* node = FindNodeByAddresss(job->GetAddress());
            if (node) {
                node->RemoveJob(job);
            }
        }

        if (IsLeader()) {
            ChunkReplicator->ScheduleChunkRefresh(chunkId);
        }

        UnregisterReplicationSinks(job);

        JobMap.Remove(jobId);

        LOG_INFO_UNLESS(IsRecovery(), "Job removed (JobId: %s)", ~ToString(jobId));
    }


    void ProcessAddedChunk(
        TDataNode* node,
        const TChunkAddInfo& chunkAddInfo,
        bool incremental)
    {
        auto nodeId = node->GetId();
        auto chunkId = FromProto<TChunkId>(chunkAddInfo.chunk_id());
        auto chunkIdWithIndex = DecodeChunkId(chunkId);
        bool cached = chunkAddInfo.cached();

        auto* chunk = FindChunk(chunkIdWithIndex.Id);
        if (!IsObjectAlive(chunk)) {
            // Nodes may still contain cached replicas of chunks that no longer exist.
            // Here we just silently ignore this case.
            if (cached) {
                return;
            }

            LOG_DEBUG_UNLESS(IsRecovery(), "Unknown chunk added, removal scheduled (NodeId: %d, Address: %s, ChunkId: %s, Cached: %s)",
                nodeId,
                ~node->GetAddress(),
                ~ToString(chunkIdWithIndex),
                ~FormatBool(cached));

            if (IsLeader()) {
                ChunkReplicator->ScheduleChunkRemoval(node, chunkId);
            }

            return;
        }

        TChunkPtrWithIndex chunkWithIndex(chunk, chunkIdWithIndex.Index);
        if (!cached && node->HasUnapprovedReplica(chunkWithIndex)) {
            LOG_DEBUG_UNLESS(IsRecovery(), "Chunk approved (NodeId: %d, Address: %s, ChunkId: %s)",
                nodeId,
                ~node->GetAddress(),
                ~ToString(chunkWithIndex));

            node->ApproveReplica(chunkWithIndex);
            return;
        }

        // Use the size reported by the node, but check it for consistency first.
        if (!chunk->ValidateChunkInfo(chunkAddInfo.chunk_info())) {
            auto error = TError("Mismatched chunk info reported by node (ChunkId: %s, Cached: %s, ExpectedInfo: {%s}, ReceivedInfo: {%s}, NodeId: %d, Address: %s)",
                ~ToString(chunkWithIndex),
                ~FormatBool(cached),
                ~chunk->ChunkInfo().DebugString(),
                ~chunkAddInfo.chunk_info().DebugString(),
                nodeId,
                ~node->GetAddress());
            LOG_ERROR(error);
            // TODO(babenko): return error to node
            return;
        }
        chunk->ChunkInfo() = chunkAddInfo.chunk_info();

        AddChunkReplica(
            node,
            chunkWithIndex,
            cached,
            incremental ? EAddReplicaReason::IncrementalHeartbeat : EAddReplicaReason::FullHeartbeat);
    }

    void ProcessRemovedChunk(
        TDataNode* node,
        const TChunkRemoveInfo& chunkInfo)
    {
        auto nodeId = node->GetId();
        auto chunkIdWithIndex = DecodeChunkId(FromProto<TChunkId>(chunkInfo.chunk_id()));
        bool cached = chunkInfo.cached();

        auto* chunk = FindChunk(chunkIdWithIndex.Id);
        if (!IsObjectAlive(chunk)) {
            LOG_DEBUG_UNLESS(IsRecovery(), "Unknown chunk replica removed (ChunkId: %s, Cached: %s, Address: %s, NodeId: %d)",
                 ~ToString(chunkIdWithIndex),
                 ~FormatBool(cached),
                 ~node->GetAddress(),
                 nodeId);
            return;
        }

        TChunkPtrWithIndex chunkWithIndex(chunk, chunkIdWithIndex.Index);
        RemoveChunkReplica(
            node,
            chunkWithIndex,
            cached,
            ERemoveReplicaReason::IncrementalHeartbeat);
    }


    TJobList* GetOrCreateJobList(const TChunkId& id)
    {
        auto* jobList = FindJobList(id);
        if (!jobList) {
            jobList = new TJobList(id);
            JobListMap.Insert(id, jobList);
        }
        return jobList;
    }

    void DropJobListIfEmpty(const TJobList* jobList)
    {
        if (jobList->Jobs().empty()) {
            JobListMap.Remove(jobList->GetChunkId());
        }
    }


    void RegisterReplicationSinks(TJob* job)
    {
        switch (job->GetType()) {
            case EJobType::Replicate: {
                FOREACH (const auto& address, job->TargetAddresses()) {
                    auto* sink = GetOrCreateReplicationSink(address);
                    YCHECK(sink->Jobs().insert(job).second);
                }
                break;
            }

            case EJobType::Remove:
                break;

            default:
                YUNREACHABLE();
        }
    }

    void UnregisterReplicationSinks(TJob* job)
    {
        switch (job->GetType()) {
            case EJobType::Replicate: {
                FOREACH (const auto& address, job->TargetAddresses()) {
                    auto* sink = GetOrCreateReplicationSink(address);
                    YCHECK(sink->Jobs().erase(job) == 1);
                    DropReplicationSinkIfEmpty(sink);
                }
                break;
            }

            case EJobType::Remove:
                break;

            default:
                YUNREACHABLE();
        }
    }

    TReplicationSink* GetOrCreateReplicationSink(const Stroka& address)
    {
        auto it = ReplicationSinkMap.find(address);
        if (it != ReplicationSinkMap.end()) {
            return &it->second;
        }

        auto pair = ReplicationSinkMap.insert(std::make_pair(address, TReplicationSink(address)));
        YCHECK(pair.second);

        return &pair.first->second;
    }

    void DropReplicationSinkIfEmpty(const TReplicationSink* sink)
    {
        if (sink->Jobs().empty()) {
            // NB: Do not try to inline this variable! erase() will destroy the object
            // and will access the key afterwards.
            auto address = sink->GetAddress();
            YCHECK(ReplicationSinkMap.erase(address) == 1);
        }
    }


    static void GetOwningNodes(
        TChunkTree* chunkTree,
        yhash_set<TChunkTree*>& visited,
        yhash_set<TCypressNodeBase*>* owningNodes)
    {
        if (!visited.insert(chunkTree).second) {
            return;
        }
        switch (chunkTree->GetType()) {
            case EObjectType::Chunk: {
                FOREACH (auto* parent, chunkTree->AsChunk()->Parents()) {
                    GetOwningNodes(parent, visited, owningNodes);
                }
                break;
            }
            case EObjectType::ChunkList: {
                auto* chunkList = chunkTree->AsChunkList();
                owningNodes->insert(chunkList->OwningNodes().begin(), chunkList->OwningNodes().end());
                FOREACH (auto* parent, chunkList->Parents()) {
                    GetOwningNodes(parent, visited, owningNodes);
                }
                break;
            }
            default:
                YUNREACHABLE();
        }
    }

};

DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Chunk, TChunk, TChunkId, ChunkMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, ChunkList, TChunkList, TChunkListId, ChunkListMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Node, TDataNode, TNodeId, NodeMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, JobList, TJobList, TChunkId, JobListMap)
DEFINE_METAMAP_ACCESSORS(TChunkManager::TImpl, Job, TJob, TJobId, JobMap)

DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunk*>, LostChunks, *ChunkReplicator);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunk*>, LostVitalChunks, *ChunkReplicator);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunk*>, OverreplicatedChunks, *ChunkReplicator);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager::TImpl, yhash_set<TChunk*>, UnderreplicatedChunks, *ChunkReplicator);

///////////////////////////////////////////////////////////////////////////////

TChunkManager::TChunkTypeHandlerBase::TChunkTypeHandlerBase(TImpl* owner)
    : TObjectTypeHandlerWithMapBase(owner->Bootstrap, &owner->ChunkMap)
    , Owner(owner)
{ }

IObjectProxyPtr TChunkManager::TChunkTypeHandlerBase::DoGetProxy(
    TChunk* chunk,
    TTransaction* transaction)
{
    UNUSED(transaction);

    return CreateChunkProxy(Bootstrap, chunk);
}

TObjectBase* TChunkManager::TChunkTypeHandlerBase::Create(
    TTransaction* transaction,
    TAccount* account,
    IAttributeDictionary* attributes,
    TReqCreateObject* request,
    TRspCreateObject* response)
{
    YCHECK(transaction);
    YCHECK(account);
    UNUSED(attributes);

    account->ValidateDiskSpaceLimit();

    auto type = GetType();
    bool isErasure = (type == EObjectType::ErasureChunk);
    const auto* requestExt = &request->GetExtension(TReqCreateChunkExt::create_chunk);

    auto erasureCodecId = isErasure ? NErasure::ECodec(requestExt->erasure_codec()) : NErasure::ECodec(NErasure::ECodec::None);
    auto* erasureCodec = isErasure ? NErasure::GetCodec(erasureCodecId) : nullptr;

    auto* chunk = Owner->CreateChunk(type);
    chunk->SetReplicationFactor(isErasure ? 1 : requestExt->replication_factor());
    chunk->SetErasureCodec(erasureCodecId);
    chunk->SetMovable(requestExt->movable());
    chunk->SetVital(requestExt->vital());
    chunk->SetStagingTransaction(transaction);
    chunk->SetStagingAccount(account);

    if (Owner->IsLeader()) {
        auto preferredHostName = requestExt->has_preferred_host_name()
            ? TNullable<Stroka>(requestExt->preferred_host_name())
            : Null;

        int replicaCount = isErasure
            ? erasureCodec->GetDataBlockCount() + erasureCodec->GetParityBlockCount()
            : requestExt->upload_replication_factor();

        auto targets = Owner->AllocateUploadTargets(replicaCount, preferredHostName);

        auto* responseExt = response->MutableExtension(TRspCreateChunkExt::create_chunk);
        TNodeDirectoryBuilder builder(responseExt->mutable_node_directory());
        TSmallVector<Stroka, TypicalReplicationFactor> targetAddresses;
        for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
            auto* target = targets[index];
            NChunkServer::TDataNodePtrWithIndex replica(
                target,
                isErasure ? index : 0);
            builder.Add(replica);
            responseExt->add_replicas(NYT::ToProto<ui32>(replica));
            targetAddresses.push_back(target->GetAddress());
        }

        LOG_DEBUG_UNLESS(Owner->IsRecovery(),
            "Allocated nodes for new chunk "
            "(ChunkId: %s, TransactionId: %s, Account: %s, Targets: [%s], "
            "PreferredHostName: %s, ReplicationFactor: %d, UploadReplicationFactor: %d, ErasureCodec: %s, Movable: %s, Vital: %s)",
            ~ToString(chunk->GetId()),
            ~ToString(transaction->GetId()),
            ~account->GetName(),
            ~JoinToString(targetAddresses),
            ~ToString(preferredHostName),
            chunk->GetReplicationFactor(),
            requestExt->upload_replication_factor(),
            ~erasureCodecId.ToString(),
            ~FormatBool(requestExt->movable()),
            ~FormatBool(requestExt->vital()));
    }

    return chunk;
}

void TChunkManager::TChunkTypeHandlerBase::DoDestroy(TChunk* chunk)
{
    Owner->DestroyChunk(chunk);
}

void TChunkManager::TChunkTypeHandlerBase::DoUnstage(
    TChunk* chunk,
    TTransaction* transaction,
    bool recursive)
{
    UNUSED(transaction);
    UNUSED(recursive);

    Owner->UnstageChunk(chunk);
}

////////////////////////////////////////////////////////////////////////////////

TChunkManager::TChunkListTypeHandler::TChunkListTypeHandler(TImpl* owner)
    : TObjectTypeHandlerWithMapBase(owner->Bootstrap, &owner->ChunkListMap)
    , Owner(owner)
{ }

IObjectProxyPtr TChunkManager::TChunkListTypeHandler::DoGetProxy(
    TChunkList* chunkList,
    TTransaction* transaction)
{
    UNUSED(transaction);

    return CreateChunkListProxy(Bootstrap, chunkList);
}

TObjectBase* TChunkManager::TChunkListTypeHandler::Create(
    TTransaction* transaction,
    TAccount* account,
    IAttributeDictionary* attributes,
    TReqCreateObject* request,
    TRspCreateObject* response)
{
    UNUSED(transaction);
    UNUSED(account);
    UNUSED(attributes);
    UNUSED(request);
    UNUSED(response);

    return Owner->CreateChunkList();
}

void TChunkManager::TChunkListTypeHandler::DoDestroy(TChunkList* chunkList)
{
    Owner->DestroyChunkList(chunkList);
}

void TChunkManager::TChunkListTypeHandler::DoUnstage(
    TChunkList* obj,
    TTransaction* transaction,
    bool recursive)
{
    Owner->UnstageChunkList(obj, transaction, recursive);
}

////////////////////////////////////////////////////////////////////////////////

TChunkManager::TChunkManager(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Impl(New<TImpl>(config, bootstrap))
{ }

void TChunkManager::Initialize()
{
    Impl->Initialize();
}

TChunkManager::~TChunkManager()
{ }

TChunkTree* TChunkManager::FindChunkTree(const TChunkTreeId& id)
{
    return Impl->FindChunkTree(id);
}

TChunkTree* TChunkManager::GetChunkTree(const TChunkTreeId& id)
{
    return Impl->GetChunkTree(id);
}

TDataNode* TChunkManager::FindNodeByAddress(const Stroka& address)
{
    return Impl->FindNodeByAddresss(address);
}

TDataNode* TChunkManager::FindNodeByHostName(const Stroka& hostName)
{
    return Impl->FindNodeByHostName(hostName);
}

const TReplicationSink* TChunkManager::FindReplicationSink(const Stroka& address)
{
    return Impl->FindReplicationSink(address);
}

TSmallVector<TDataNode*, TypicalReplicationFactor> TChunkManager::AllocateUploadTargets(
    int replicaCount,
    const TNullable<Stroka>& preferredHostName)
{
    return Impl->AllocateUploadTargets(replicaCount, preferredHostName);
}

TMutationPtr TChunkManager::CreateRegisterNodeMutation(
    const TMetaReqRegisterNode& request)
{
    return Impl->CreateRegisterNodeMutation(request);
}

TMutationPtr TChunkManager::CreateUnregisterNodeMutation(
    const TMetaReqUnregisterNode& request)
{
    return Impl->CreateUnregisterNodeMutation(request);
}

TMutationPtr TChunkManager::CreateFullHeartbeatMutation(
    TCtxFullHeartbeatPtr context)
{
    return Impl->CreateFullHeartbeatMutation(context);
}

TMutationPtr TChunkManager::CreateIncrementalHeartbeatMutation(
    const TMetaReqIncrementalHeartbeat& request)
{
    return Impl->CreateIncrementalHeartbeatMutation(request);
}

TMutationPtr TChunkManager::CreateUpdateJobsMutation(
    const TMetaReqUpdateJobs& request)
{
    return Impl->CreateUpdateJobsMutation(request);
}

TMutationPtr TChunkManager::CreateUpdateChunkReplicationFactorMutation(
    const NProto::TMetaReqUpdateChunkReplicationFactor& request)
{
    return Impl->CreateUpdateChunkReplicationFactorMutation(request);
}

TChunk* TChunkManager::CreateChunk(EObjectType type)
{
    return Impl->CreateChunk(type);
}

TChunkList* TChunkManager::CreateChunkList()
{
    return Impl->CreateChunkList();
}

void TChunkManager::ConfirmChunk(
    TChunk* chunk,
    const std::vector<NChunkClient::TChunkReplica>& replicas,
    NChunkClient::NProto::TChunkInfo* chunkInfo,
    NChunkClient::NProto::TChunkMeta* chunkMeta)
{
    Impl->ConfirmChunk(
        chunk,
        replicas,
        chunkInfo,
        chunkMeta);
}

void TChunkManager::AttachToChunkList(
    TChunkList* chunkList,
    TChunkTree** childrenBegin,
    TChunkTree** childrenEnd,
    bool resetSorted)
{
    Impl->AttachToChunkList(chunkList, childrenBegin, childrenEnd, resetSorted);
}

void TChunkManager::AttachToChunkList(
    TChunkList* chunkList,
    const std::vector<TChunkTree*>& children,
    bool resetSorted)
{
    Impl->AttachToChunkList(chunkList, children, resetSorted);
}

void TChunkManager::AttachToChunkList(
    TChunkList* chunkList,
    TChunkTree* child,
    bool resetSorted)
{
    Impl->AttachToChunkList(chunkList, child, resetSorted);
}

void TChunkManager::RebalanceChunkTree(TChunkList* chunkList)
{
    Impl->RebalanceChunkTree(chunkList);
}

void TChunkManager::ClearChunkList(TChunkList* chunkList)
{
    Impl->ClearChunkList(chunkList);
}

void TChunkManager::ScheduleJobs(
    TDataNode* node,
    const std::vector<TJobInfo>& runningJobs,
    std::vector<TJobStartInfo>* jobsToStart,
    std::vector<TJobStopInfo>* jobsToStop)
{
    Impl->ScheduleJobs(
        node,
        runningJobs,
        jobsToStart,
        jobsToStop);
}

bool TChunkManager::IsReplicatorEnabled()
{
    return Impl->IsReplicatorEnabled();
}

void TChunkManager::ScheduleRFUpdate(TChunkTree* chunkTree)
{
    Impl->ScheduleRFUpdate(chunkTree);
}

TDataNodePtrWithIndexList TChunkManager::GetChunkReplicas(const TChunk* chunk)
{
    return Impl->GetChunkReplicas(chunk);
}

TTotalNodeStatistics TChunkManager::GetTotalNodeStatistics()
{
    return Impl->GetTotalNodeStatistics();
}

bool TChunkManager::IsNodeConfirmed(const TDataNode* node)
{
    return Impl->IsNodeConfirmed(node);
}

int TChunkManager::GetChunkReplicaCount()
{
    return Impl->GetChunkReplicaCount();
}

int TChunkManager::GetRegisteredNodeCount()
{
    return Impl->GetRegisteredNodeCount();
}

std::vector<TYPath> TChunkManager::GetOwningNodes(TChunkTree* chunkTree)
{
    return Impl->GetOwningNodes(chunkTree);
}

DELEGATE_METAMAP_ACCESSORS(TChunkManager, Chunk, TChunk, TChunkId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, ChunkList, TChunkList, TChunkListId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, Node, TDataNode, TNodeId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, JobList, TJobList, TChunkId, *Impl)
DELEGATE_METAMAP_ACCESSORS(TChunkManager, Job, TJob, TJobId, *Impl)

DELEGATE_SIGNAL(TChunkManager, void(const TDataNode*), NodeRegistered, *Impl);
DELEGATE_SIGNAL(TChunkManager, void(const TDataNode*), NodeUnregistered, *Impl);

DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunk*>, LostChunks, *Impl);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunk*>, LostVitalChunks, *Impl);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunk*>, OverreplicatedChunks, *Impl);
DELEGATE_BYREF_RO_PROPERTY(TChunkManager, yhash_set<TChunk*>, UnderreplicatedChunks, *Impl);

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
