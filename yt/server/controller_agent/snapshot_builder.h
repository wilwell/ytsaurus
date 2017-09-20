#pragma once

#include "public.h"
#include "private.h"

#include <yt/server/cell_scheduler/public.h>

#include <yt/server/misc/fork_executor.h>

#include <yt/ytlib/api/public.h>

#include <yt/core/pipes/pipe.h>

#include <yt/core/profiling/profiler.h>

#include <util/system/file.h>

namespace NYT {
namespace NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

struct TSnapshotJob
    : public TIntrinsicRefCounted
{
    NScheduler::TOperationPtr Operation;
    NControllerAgent::IOperationControllerPtr Controller;
    NPipes::TAsyncReaderPtr Reader;
    std::unique_ptr<TFile> OutputFile;
    //! The length of recent completed jobs prefix that may be safely removed after saving this snapshot.
    //! (i.e. their progress won't be lost if we restore from this snapshot).
    int NumberOfJobsToRelease = 0;
    bool Suspended = false;
};

DEFINE_REFCOUNTED_TYPE(TSnapshotJob)

////////////////////////////////////////////////////////////////////////////////

class TSnapshotBuilder
    : public TForkExecutor
{
public:
    TSnapshotBuilder(
        TSchedulerConfigPtr config,
        NScheduler::TSchedulerPtr scheduler,
        NApi::IClientPtr client);

    TFuture<void> Run();

private:
    const TSchedulerConfigPtr Config_;
    const NScheduler::TSchedulerPtr Scheduler_;
    const NApi::IClientPtr Client_;

    std::vector<TSnapshotJobPtr> Jobs_;

    NProfiling::TProfiler Profiler;

    virtual TDuration GetTimeout() const override;
    virtual void RunParent() override;
    virtual void RunChild() override;

    TFuture<std::vector<TError>> UploadSnapshots();
    void UploadSnapshot(const TSnapshotJobPtr& job);

    bool ControllersSuspended_ = false;
};

DEFINE_REFCOUNTED_TYPE(TSnapshotBuilder)

////////////////////////////////////////////////////////////////////////////////

} // namespace NControllerAgent
} // namespace NYT
