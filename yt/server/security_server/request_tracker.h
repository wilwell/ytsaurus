#pragma once

#include "public.h"

#include <core/concurrency/periodic_executor.h>
#include <core/concurrency/thread_affinity.h>
#include <core/misc/error.h>

#include <server/cell_master/public.h>

#include <server/security_server/security_manager.pb.h>

namespace NYT {
namespace NSecurityServer {

////////////////////////////////////////////////////////////////////////////////

class TRequestTracker
    : public TRefCounted
{
public:
    explicit TRequestTracker(
        TSecurityManagerConfigPtr config,
        NCellMaster::TBootstrap* bootstrap);

    void StartFlush();
    void StopFlush();

    void ChargeUser(TUser* user, int requestCount);

private:
    TSecurityManagerConfigPtr Config;
    NCellMaster::TBootstrap* Bootstrap;

    NProto::TReqUpdateRequestStatistics UpdateRequestStatisticsRequest;
    std::vector<TUser*> UsersWithRequestStatisticsUpdate;

    NConcurrency::TPeriodicExecutorPtr FlushExecutor;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);


    void Reset();
    void OnFlush();

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NSecurityServer
} // namespace NYT
