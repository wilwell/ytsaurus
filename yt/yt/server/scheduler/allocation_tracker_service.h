#pragma once

#include "public.h"

#include <yt/yt/core/rpc/public.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateAllocationTrackerService(TBootstrap* bootstrap);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
