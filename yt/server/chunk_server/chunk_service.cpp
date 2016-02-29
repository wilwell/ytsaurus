#include "chunk_service.h"
#include "private.h"
#include "chunk.h"
#include "chunk_manager.h"
#include "helpers.h"
#include "chunk_owner_base.h"

#include <yt/server/cell_master/bootstrap.h>
#include <yt/server/cell_master/hydra_facade.h>
#include <yt/server/cell_master/master_hydra_service.h>

#include <yt/server/node_tracker_server/node.h>
#include <yt/server/node_tracker_server/node_directory_builder.h>
#include <yt/server/node_tracker_server/node_tracker.h>

#include <yt/server/transaction_server/transaction.h>

#include <yt/ytlib/chunk_client/chunk_service_proxy.h>

#include <yt/core/erasure/codec.h>

namespace NYT {
namespace NChunkServer {

using namespace NHydra;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NNodeTrackerServer;
using namespace NObjectServer;
using namespace NCellMaster;
using namespace NHydra;
using namespace NTransactionClient;
using namespace NRpc;

using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

class TChunkService
    : public NCellMaster::TMasterHydraServiceBase
{
public:
    explicit TChunkService(TBootstrap* bootstrap)
        : TMasterHydraServiceBase(
            bootstrap,
            TChunkServiceProxy::GetServiceName(),
            ChunkServerLogger,
            TChunkServiceProxy::GetProtocolVersion())
    {
        RegisterMethod(RPC_SERVICE_METHOD_DESC(LocateChunks)
            .SetInvoker(GetGuardedAutomatonInvoker(EAutomatonThreadQueue::ChunkLocator))
            .SetHeavy(true));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(AllocateWriteTargets)
            .SetInvoker(GetGuardedAutomatonInvoker(EAutomatonThreadQueue::ChunkLocator)));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ExportChunks)
            .SetHeavy(true));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ImportChunks)
            .SetHeavy(true));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(GetChunkOwningNodes));
        RegisterMethod(RPC_SERVICE_METHOD_DESC(ExecuteBatch)
            .SetHeavy(true)
            .SetMaxQueueSize(10000)
            .SetMaxConcurrency(10000));
    }

private:
    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, LocateChunks)
    {
        ValidatePeer(EPeerKind::LeaderOrFollower);

        context->SetRequestInfo("ChunkCount: %v",
            request->chunk_ids_size());

        auto chunkManager = Bootstrap_->GetChunkManager();
        TNodeDirectoryBuilder nodeDirectoryBuilder(response->mutable_node_directory());

        for (const auto& protoChunkId : request->chunk_ids()) {
            auto chunkId = FromProto<TChunkId>(protoChunkId);
            auto chunkIdWithIndex = DecodeChunkId(chunkId);

            auto* chunk = chunkManager->FindChunk(chunkIdWithIndex.Id);
            if (!IsObjectAlive(chunk))
                continue;

            TChunkPtrWithIndex chunkWithIndex(chunk, chunkIdWithIndex.Index);
            auto replicas = chunkManager->LocateChunk(chunkWithIndex);

            auto* info = response->add_chunks();
            ToProto(info->mutable_chunk_id(), chunkId);
            ToProto(info->mutable_replicas(), replicas);

            for (auto replica : replicas) {
                nodeDirectoryBuilder.Add(replica);
            }
        }

        context->SetResponseInfo("ChunkCount: %v",
            response->chunks_size());
        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, AllocateWriteTargets)
    {
        ValidatePeer(EPeerKind::Leader);

        context->SetRequestInfo("SubrequestCount: %v",
            request->subrequests_size());

        auto chunkManager = Bootstrap_->GetChunkManager();
        auto nodeTracker = Bootstrap_->GetNodeTracker();

        TNodeDirectoryBuilder builder(response->mutable_node_directory());

        for (const auto& subrequest : request->subrequests()) {
            auto chunkId = FromProto<TChunkId>(subrequest.chunk_id());
            int desiredTargetCount = subrequest.desired_target_count();
            int minTargetCount = subrequest.min_target_count();
            auto replicationFactorOverride = subrequest.has_replication_factor_override()
                ? MakeNullable(subrequest.replication_factor_override())
                : Null;
            auto preferredHostName = subrequest.has_preferred_host_name()
                ? MakeNullable(subrequest.preferred_host_name())
                : Null;
            const auto& forbiddenAddresses = subrequest.forbidden_addresses();

            auto* chunk = chunkManager->GetChunkOrThrow(chunkId);

            TNodeList forbiddenNodes;
            for (const auto& address : forbiddenAddresses) {
                auto* node = nodeTracker->FindNodeByAddress(address);
                if (node) {
                    forbiddenNodes.push_back(node);
                }
            }
            std::sort(forbiddenNodes.begin(), forbiddenNodes.end());

            auto targets = chunkManager->AllocateWriteTargets(
                chunk,
                desiredTargetCount,
                minTargetCount,
                replicationFactorOverride,
                &forbiddenNodes,
                preferredHostName);

            auto* subresponse = response->add_subresponses();
            for (int index = 0; index < static_cast<int>(targets.size()); ++index) {
                auto* target = targets[index];
                auto replica = TNodePtrWithIndex(target, GenericChunkReplicaIndex);
                builder.Add(replica);
                subresponse->add_replicas(ToProto<ui32>(replica));
            }

            LOG_DEBUG("Write targets allocated "
                "(ChunkId: %v, DesiredTargetCount: %v, MinTargetCount: %v, ReplicationFactorOverride: %v, "
                "PreferredHostName: %v, ForbiddenAddresses: %v, Targets: %v",
                chunkId,
                desiredTargetCount,
                minTargetCount,
                replicationFactorOverride,
                preferredHostName,
                forbiddenAddresses,
                MakeFormattableRange(targets, TNodePtrAddressFormatter()));
        }

        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, ExportChunks)
    {
        ValidatePeer(EPeerKind::Leader);
        SyncWithUpstream();

        auto transactionId = FromProto<TTransactionId>(request->transaction_id());

        context->SetRequestInfo("TransactionId: %v, ChunkCount: %v",
            transactionId,
            request->chunks_size());

        auto chunkManager = Bootstrap_->GetChunkManager();
        chunkManager
            ->CreateExportChunksMutation(context)
            ->CommitAndReply(context);
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, ImportChunks)
    {
        ValidatePeer(EPeerKind::Leader);
        SyncWithUpstream();

        auto transactionId = FromProto<TTransactionId>(request->transaction_id());

        context->SetRequestInfo("TransactionId: %v, ChunkCount: %v",
            transactionId,
            request->chunks_size());

        auto chunkManager = Bootstrap_->GetChunkManager();
        chunkManager
            ->CreateImportChunksMutation(context)
            ->CommitAndReply(context);
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, GetChunkOwningNodes)
    {
        ValidatePeer(EPeerKind::LeaderOrFollower);
        SyncWithUpstream();

        auto chunkId = FromProto<TChunkId>(request->chunk_id());

        context->SetRequestInfo("ChunkId: %v",
            chunkId);

        auto chunkManager = Bootstrap_->GetChunkManager();
        auto* chunk = chunkManager->GetChunkOrThrow(chunkId);

        auto owningNodes = GetOwningNodes(chunk);
        for (const auto* node : owningNodes) {
            auto* protoNode = response->add_nodes();
            ToProto(protoNode->mutable_node_id(), node->GetId());
            if (node->GetTransaction()) {
                ToProto(protoNode->mutable_transaction_id(), node->GetTransaction()->GetId());
            }
        }

        context->SetResponseInfo("NodeCount: %v",
            response->nodes_size());
        context->Reply();
    }

    DECLARE_RPC_SERVICE_METHOD(NChunkClient::NProto, ExecuteBatch)
    {
        ValidatePeer(EPeerKind::Leader);
        SyncWithUpstream();

        context->SetRequestInfo(
            "CreateSubrequestCount: %v, "
            "ConfirmSubrequestCount: %v",
            request->create_subrequests_size(),
            request->confirm_subrequests_size());

        auto chunkManager = Bootstrap_->GetChunkManager();
        chunkManager
            ->CreateExecuteBatchMutation(context)
            ->CommitAndReply(context);
    }
};

IServicePtr CreateChunkService(TBootstrap* boostrap)
{
    return New<TChunkService>(boostrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkServer
} // namespace NYT
