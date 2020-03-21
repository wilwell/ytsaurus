#pragma once

#include "public.h"

namespace NYT::NTransactionClient {

////////////////////////////////////////////////////////////////////////////////

ITimestampProviderPtr CreateBatchingTimestampProvider(
    ITimestampProviderPtr underlying,
    TDuration batchPeriod);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionClient
