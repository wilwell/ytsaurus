#pragma once

#include "public.h"
#include "exec_node.h"

#include <yt/yt/client/chunk_client/data_statistics.h>

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node.pb.h>

#include <yt/yt/server/lib/scheduler/structs.h>

#include <yt/yt/ytlib/chunk_client/legacy_data_slice.h>

#include <yt/yt/core/actions/callback.h>

#include <yt/yt/core/misc/optional.h>
#include <yt/yt/core/misc/property.h>
#include <yt/yt/core/misc/phoenix.h>

#include <yt/yt/core/yson/consumer.h>

#include <yt/yt/library/vector_hdrf/job_resources.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

class TJob
    : public TRefCounted
{
    DEFINE_BYVAL_RO_PROPERTY(TJobId, Id);

    DEFINE_BYVAL_RO_PROPERTY(EJobType, Type);

    //! The id of operation the job belongs to.
    DEFINE_BYVAL_RO_PROPERTY(TOperationId, OperationId);

    //! The incarnation of the controller agent responsible for this job.
    DEFINE_BYVAL_RO_PROPERTY(TIncarnationId, IncarnationId);

    //! The epoch of the controller of operation job belongs to.
    DEFINE_BYVAL_RW_PROPERTY(TControllerEpoch, ControllerEpoch);

    //! Exec node where the job is running.
    DEFINE_BYVAL_RW_PROPERTY(TExecNodePtr, Node);

    //! Node id obtained from corresponding joblet during the revival process.
    DEFINE_BYVAL_RO_PROPERTY(NNodeTrackerClient::TNodeId, RevivalNodeId, NNodeTrackerClient::InvalidNodeId);

    //! Node address obtained from corresponding joblet during the revival process.
    DEFINE_BYVAL_RO_PROPERTY(TString, RevivalNodeAddress);

    //! The time when the job was started.
    DEFINE_BYVAL_RO_PROPERTY(TInstant, StartTime);

    //! True if job can be interrupted.
    DEFINE_BYVAL_RO_PROPERTY(bool, Interruptible);

    //! The time when the job was finished.
    DEFINE_BYVAL_RW_PROPERTY(std::optional<TInstant>, FinishTime);

    //! True if job was already unregistered.
    DEFINE_BYVAL_RW_PROPERTY(bool, Unregistered, false);

    //! Current state of the job.
    DEFINE_BYVAL_RW_PROPERTY(EJobState, State, EJobState::None);

    //! Fair-share tree this job belongs to.
    DEFINE_BYVAL_RO_PROPERTY(TString, TreeId);

    //! Abort reason saved if job was aborted.
    DEFINE_BYVAL_RW_PROPERTY(EAbortReason, AbortReason);

    DEFINE_BYREF_RW_PROPERTY(TJobResources, ResourceUsage);
    DEFINE_BYREF_RO_PROPERTY(TJobResources, ResourceLimits);

    //! Temporary flag used during heartbeat jobs processing to mark found jobs.
    DEFINE_BYVAL_RW_PROPERTY(bool, FoundOnNode);

    //! Preemption mode which says how to preempt job.
    DEFINE_BYVAL_RO_PROPERTY(EPreemptionMode, PreemptionMode);

    //! Index of operation when job was scheduled.
    DEFINE_BYVAL_RO_PROPERTY(int, SchedulingIndex);

    //! Stage job was scheduled at.
    DEFINE_BYVAL_RO_PROPERTY(std::optional<EJobSchedulingStage>, SchedulingStage);

    //! Flag that marks job as preempted by scheduler.
    DEFINE_BYVAL_RW_PROPERTY(bool, Preempted, false);

    //! Job fail was requested by scheduler.
    DEFINE_BYVAL_RW_PROPERTY(bool, FailRequested, false);

    //! String describing preemption reason.
    DEFINE_BYVAL_RW_PROPERTY(TString, PreemptionReason);

    //! Preemptor job id and operation id.
    DEFINE_BYVAL_RW_PROPERTY(std::optional<TPreemptedFor>, PreemptedFor);

    //! The purpose of the job interruption.
    DEFINE_BYVAL_RW_PROPERTY(EInterruptReason, InterruptReason, EInterruptReason::None);

    //! Deadline for job to be interrupted.
    DEFINE_BYVAL_RW_PROPERTY(NProfiling::TCpuInstant, InterruptDeadline, 0);

    //! Deadline for running job.
    DEFINE_BYVAL_RW_PROPERTY(NProfiling::TCpuInstant, RunningJobUpdateDeadline, 0);

    //! True for revived job that was not confirmed by a heartbeat from the corresponding node yet.
    DEFINE_BYVAL_RW_PROPERTY(bool, WaitingForConfirmation, false);

    //! Job execution duration as reported by the node.
    DEFINE_BYVAL_RW_PROPERTY(TDuration, ExecDuration);

public:
    TJob(
        TJobId id,
        EJobType type,
        TOperationId operationId,
        TIncarnationId incarnationId,
        TControllerEpoch controllerEpoch,
        TExecNodePtr node,
        TInstant startTime,
        const TJobResources& resourceLimits,
        bool interruptible,
        EPreemptionMode preemptionMode,
        TString treeId,
        int schedulingIndex,
        std::optional<EJobSchedulingStage> schedulingStage = std::nullopt,
        NNodeTrackerClient::TNodeId revivalNodeId = NNodeTrackerClient::InvalidNodeId,
        TString revivalNodeAddress = TString());

    //! The difference between |FinishTime| and |StartTime|.
    TDuration GetDuration() const;

    //! Returns true if the job was revived.
    bool IsRevived() const;
};

DEFINE_REFCOUNTED_TYPE(TJob)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
