#include "cpu_monitor.h"
#include "job_proxy.h"

#include <yt/core/concurrency/periodic_executor.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

TCpuMonitor::TCpuMonitor(
    NScheduler::TJobCpuMonitorConfigPtr config,
    IInvokerPtr invoker,
    TJobProxy* jobProxy,
    const double hardCpuLimit)
    : Config_(std::move(config))
    , MonitoringExecutor_(New<NConcurrency::TPeriodicExecutor>(
        invoker,
        BIND(&TCpuMonitor::DoCheck, MakeWeak(this)),
        Config_->CheckPeriod))
    , JobProxy_(jobProxy)
    , HardLimit_(hardCpuLimit)
    , SoftLimit_(hardCpuLimit)
    , Logger("CpuMonitor")
{ }

void TCpuMonitor::Start()
{
    MonitoringExecutor_->Start();
}

TFuture<void> TCpuMonitor::Stop()
{
    return MonitoringExecutor_->Stop();
}

void TCpuMonitor::FillStatistics(NJobTrackerClient::TStatistics& statistics) const
{
    if (SmoothedUsage_) {
        statistics.AddSample("/job_proxy/smoothed_cpu_usage_x100", static_cast<i64>(*SmoothedUsage_ * 100));
        statistics.AddSample("/job_proxy/preemptable_cpu_x100", static_cast<i64>((HardLimit_ - SoftLimit_) * 100));

        statistics.AddSample("/job_proxy/aggregated_smoothed_cpu_usage_x100", static_cast<i64>(AggregatedSmoothedCpuUsage_ * 100));
        statistics.AddSample("/job_proxy/aggregated_max_cpu_usage_x100", static_cast<i64>(AggregatedMaxCpuUsage_ * 100));
        statistics.AddSample("/job_proxy/aggregated_preemptable_cpu_x100", static_cast<i64>(AggregatedPreemptableCpu_ * 100));
    }
}

void TCpuMonitor::DoCheck()
{
    if (!TryUpdateSmoothedValue()) {
        return;
    }
    UpdateVotes();

    auto decision = TryMakeDecision();
    if (decision) {
        YT_LOG_DEBUG("Soft limit changed (OldValue: %v, NewValue: %v)",
            SoftLimit_,
            *decision);
        SoftLimit_ = *decision;
        if (Config_->EnableCpuReclaim) {
            JobProxy_->SetCpuLimit(*decision);
        }
    }

    UpdateAggregates();
};

bool TCpuMonitor::TryUpdateSmoothedValue()
{
    TDuration totalCpu;
    try {
        totalCpu = JobProxy_->GetSpentCpuTime();
    } catch (const std::exception& ex) {
        YT_LOG_ERROR(ex, "Failed to get CPU statistics");
        return false;
    }

    auto now = TInstant::Now();
    bool canCalcSmoothedUsage = LastCheckTime_ && LastTotalCpu_;
    if (canCalcSmoothedUsage) {
        CheckedTimeInterval_ = (now - *LastCheckTime_);
        auto deltaCpu = totalCpu - *LastTotalCpu_;
        auto cpuUsage = deltaCpu / *CheckedTimeInterval_;
        auto newSmoothedUsage = SmoothedUsage_
            ? Config_->SmoothingFactor * cpuUsage + (1 - Config_->SmoothingFactor) * (*SmoothedUsage_)
            : HardLimit_;
        YT_LOG_DEBUG("Smoothed CPU usage updated (OldValue: %v, NewValue: %v)",
            SmoothedUsage_,
            newSmoothedUsage);
        SmoothedUsage_ = newSmoothedUsage;
    }
    LastCheckTime_ = now;
    LastTotalCpu_ = totalCpu;
    return canCalcSmoothedUsage;
}

void TCpuMonitor::UpdateVotes()
{
    double ratio = *SmoothedUsage_ / SoftLimit_;

    if (ratio < Config_->RelativeLowerBound) {
        Votes_.emplace_back(ECpuMonitorVote::Decrease);
    } else if (ratio > Config_->RelativeUpperBound) {
        Votes_.emplace_back(ECpuMonitorVote::Increase);
    } else {
        Votes_.emplace_back(ECpuMonitorVote::Keep);
    }
}

std::optional<double> TCpuMonitor::TryMakeDecision()
{
    std::optional<double> result;
    if (Votes_.size() >= Config_->VoteWindowSize) {
        auto voteSum = 0;
        for (const auto vote : Votes_) {
            if (vote == ECpuMonitorVote::Increase) {
                ++voteSum;
            } else if (vote == ECpuMonitorVote::Decrease) {
                --voteSum;
            }
        }
        if (voteSum > Config_->VoteDecisionThreshold) {
            auto softLimit = std::min(SoftLimit_ * Config_->IncreaseCoefficient, HardLimit_);
            if (softLimit != SoftLimit_) {
                result = softLimit;
            }
            Votes_.clear();
        } else if (voteSum < -Config_->VoteDecisionThreshold) {
            auto softLimit = std::max(SoftLimit_ * Config_->DecreaseCoefficient, Config_->MinCpuLimit);
            if (softLimit != SoftLimit_) {
                result = softLimit;
            }
            Votes_.clear();
        } else {
            Votes_.pop_front();
        }
    }
    return result;
}

void TCpuMonitor::UpdateAggregates()
{
    if (!CheckedTimeInterval_ || !SmoothedUsage_) {
        return;
    }
    double seconds = static_cast<double>(CheckedTimeInterval_->MicroSeconds()) / 1000 / 1000;
    AggregatedSmoothedCpuUsage_ += *SmoothedUsage_ * seconds;
    AggregatedPreemptableCpu_ += (HardLimit_ - SoftLimit_) * seconds;
    AggregatedMaxCpuUsage_ += HardLimit_ * seconds;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
