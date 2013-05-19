#pragma once

#include "common.h"

#include <ytlib/actions/future.h>

#include <ytlib/ytree/yson_serializable.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////
   
// TODO(babenko): move to public.h
class TThroughputThrottlerConfig;
typedef TIntrusivePtr<TThroughputThrottlerConfig> TThroughputThrottlerConfigPtr;

class IThroughputThrottler;
typedef TIntrusivePtr<IThroughputThrottler> IThroughputThrottlerPtr;

class TThroughputThrottlerConfig
    : public TYsonSerializable
{
public:
    TThroughputThrottlerConfig()
    {
        RegisterParameter("period", Period)
            .Default(TDuration::Seconds(5));
        RegisterParameter("limit", Limit)
            .Default(Null)
            .GreaterThanOrEqual(0);
    }

    //! Period for which the bandwidth limit applies.
    TDuration Period;

    //! Limit on average throughput (per sec). Null means unlimited.
    TNullable<i64> Limit;
};

//! Enables async operation to throttle based on throughput limit.
/*!
 *  This interface and its implementations are vastly inspired by |DataTransferThrottler| class from Hadoop
 *  but return promise instead of using direct sleep calls.
 */
struct IThroughputThrottler
    : public virtual TRefCounted
{
    //! Assuming that we are about to transfer #count bytes,
    //! returns a future that is set when enough time has passed
    //! to ensure proper bandwidth utilization.
    /*!
     *  \note Thread affinity: any
     */
    virtual TFuture<void> Throttle(i64 count) = 0;
};

IThroughputThrottlerPtr CreateThrottler(TThroughputThrottlerConfigPtr config);
IThroughputThrottlerPtr GetUnlimitedThrottler();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT

