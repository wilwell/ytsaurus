#include "job_registry.h"

#include "chunk_manager.h"
#include "config.h"
#include "job.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/config.h>
#include <yt/yt/server/master/cell_master/config_manager.h>

#include <yt/yt/server/master/node_tracker_server/data_center.h>
#include <yt/yt/server/master/node_tracker_server/node.h>
#include <yt/yt/server/master/node_tracker_server/node_tracker.h>

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt/ytlib/node_tracker_client/helpers.h>

#include <yt/yt/core/concurrency/throughput_throttler.h>

namespace NYT::NChunkServer {

using namespace NNodeTrackerClient::NProto;
using namespace NNodeTrackerServer;
using namespace NCellMaster;
using namespace NConcurrency;
using namespace NProfiling;
using namespace NCypressClient;
using namespace NObjectClient;

////////////////////////////////////////////////////////////////////////////////

static const auto& Logger = ChunkServerLogger;

////////////////////////////////////////////////////////////////////////////////

TJobRegistry::TJobRegistry(
    TChunkManagerConfigPtr config,
    TBootstrap* bootstrap)
    : Config_(config)
    , Bootstrap_(bootstrap)
    , JobThrottler_(CreateReconfigurableThroughputThrottler(
        New<TThroughputThrottlerConfig>(),
        ChunkServerLogger,
        ChunkServerProfilerRegistry.WithPrefix("/job_throttler")))
{ }

TJobRegistry::~TJobRegistry() = default;

void TJobRegistry::Start()
{
    const auto& configManager = Bootstrap_->GetConfigManager();
    configManager->SubscribeConfigChanged(DynamicConfigChangedCallback_);
}

void TJobRegistry::Stop()
{
    const auto& configManager = Bootstrap_->GetConfigManager();
    configManager->UnsubscribeConfigChanged(DynamicConfigChangedCallback_);
}

void TJobRegistry::RegisterJob(const TJobPtr& job)
{
    job->GetNode()->RegisterJob(job);

    auto jobType = job->GetType();
    ++RunningJobs_[jobType];
    ++JobsStarted_[jobType];

    const auto& chunkManager = Bootstrap_->GetChunkManager();
    auto chunkId = job->GetChunkIdWithIndexes().Id;
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (chunk) {
        chunk->AddJob(job);
    }

    JobThrottler_->Acquire(1);

    YT_LOG_DEBUG("Job registered (JobId: %v, JobType: %v, Address: %v)",
        job->GetJobId(),
        job->GetType(),
        job->GetNode()->GetDefaultAddress());
}

void TJobRegistry::UnregisterJob(TJobPtr job)
{
    job->GetNode()->UnregisterJob(job);
    auto jobType = job->GetType();
    --RunningJobs_[jobType];

    auto jobState = job->GetState();
    switch (jobState) {
        case EJobState::Completed:
            ++JobsCompleted_[jobType];
            break;
        case EJobState::Failed:
            ++JobsFailed_[jobType];
            break;
        case EJobState::Aborted:
            ++JobsAborted_[jobType];
            break;
        default:
            break;
    }
    const auto& chunkManager = Bootstrap_->GetChunkManager();
    auto chunkId = job->GetChunkIdWithIndexes().Id;
    auto* chunk = chunkManager->FindChunk(chunkId);
    if (chunk) {
        chunk->RemoveJob(job);
        chunkManager->ScheduleChunkRefresh(chunk);
    }

    YT_LOG_DEBUG("Job unregistered (JobId: %v, JobType: %v, Address: %v)",
        job->GetJobId(),
        job->GetType(),
        job->GetNode()->GetDefaultAddress());
}

bool TJobRegistry::IsOverdraft() const
{
    return JobThrottler_->IsOverdraft();
}

const TDynamicChunkManagerConfigPtr& TJobRegistry::GetDynamicConfig()
{
    const auto& configManager = Bootstrap_->GetConfigManager();
    return configManager->GetConfig()->ChunkManager;
}

void TJobRegistry::OnDynamicConfigChanged(TDynamicClusterConfigPtr /*oldConfig*/)
{
    JobThrottler_->Reconfigure(GetDynamicConfig()->JobThrottler);
}

void TJobRegistry::OverrideResourceLimits(TNodeResources* resourceLimits, const TNode& node)
{
    const auto& resourceLimitsOverrides = node.ResourceLimitsOverrides();
    #define XX(name, Name) \
        if (resourceLimitsOverrides.has_##name()) { \
            resourceLimits->set_##name(std::min(resourceLimitsOverrides.name(), resourceLimits->name())); \
        }
    ITERATE_NODE_RESOURCE_LIMITS_OVERRIDES(XX)
    #undef XX
}

void TJobRegistry::OnProfiling(TSensorBuffer* buffer) const
{
    for (auto jobType : TEnumTraits<EJobType>::GetDomainValues()) {
        if (jobType >= NJobTrackerClient::FirstMasterJobType && jobType <= NJobTrackerClient::LastMasterJobType) {
            buffer->PushTag({"job_type", FormatEnum(jobType)});

            buffer->AddGauge("/running_job_count", RunningJobs_[jobType]);
            buffer->AddCounter("/jobs_started", JobsStarted_[jobType]);
            buffer->AddCounter("/jobs_completed", JobsCompleted_[jobType]);
            buffer->AddCounter("/jobs_failed", JobsFailed_[jobType]);
            buffer->AddCounter("/jobs_aborted", JobsAborted_[jobType]);

            buffer->PopTag();
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChunkServer
