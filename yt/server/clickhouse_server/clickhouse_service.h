#pragma once

#include <yt/core/rpc/public.h>

namespace NYT::NClickHouseServer {

////////////////////////////////////////////////////////////////////////////////

NRpc::IServicePtr CreateClickHouseService(IInvokerPtr invoker);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
