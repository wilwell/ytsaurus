#pragma once

#include "public.h"

#include <yt/core/ytree/yson_serializable.h>

#include <yt/core/misc/config.h>

namespace NYT {
namespace NNet {

////////////////////////////////////////////////////////////////////////////////

class TDialerConfig
    : public NYTree::TYsonSerializable
{
public:
    int Priority;
    bool EnableNoDelay;
    bool EnableAggressiveReconnect;

    TDuration MinRto;
    TDuration MaxRto;
    double RtoScale;

    TDialerConfig()
    {
        RegisterParameter("priority", Priority)
            .InRange(0, 6)
            .Default(0);
        RegisterParameter("enable_no_delay", EnableNoDelay)
            .Default(true);
        RegisterParameter("enable_aggressive_reconnect", EnableAggressiveReconnect)
            .Default(false);
        RegisterParameter("min_rto", MinRto)
            .Default(TDuration::MilliSeconds(100));
        RegisterParameter("max_rto", MaxRto)
            .Default(TDuration::Seconds(30));
        RegisterParameter("rto_scale", RtoScale)
            .GreaterThan(0.0)
            .Default(2.0);
        
        RegisterValidator([&] () {
            if (MaxRto < MinRto) {
                THROW_ERROR_EXCEPTION("\"max_rto\" should be greater than or equal to \"min_rto\"");
            }
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TDialerConfig)

////////////////////////////////////////////////////////////////////////////////

//! Configuration for TAddressResolver singleton.
class TAddressResolverConfig
    : public TExpiringCacheConfig
{
public:
    bool EnableIPv4;
    bool EnableIPv6;
    TNullable<TString> LocalHostFqdn;
    int Retries;
    TDuration ResolveTimeout;
    TDuration MaxResolveTimeout;
    TDuration WarningTimeout;

    TAddressResolverConfig()
    {
        RegisterParameter("enable_ipv4", EnableIPv4)
            .Default(true);
        RegisterParameter("enable_ipv6", EnableIPv6)
            .Default(true);
        RegisterParameter("localhost_fqdn", LocalHostFqdn)
            .Default();
        RegisterParameter("retries", Retries)
            .Default(25);
        RegisterParameter("resolve_timeout", ResolveTimeout)
            .Default(TDuration::MilliSeconds(500));
        RegisterParameter("max_resolve_timeout", MaxResolveTimeout)
            .Default(TDuration::MilliSeconds(5000));
        RegisterParameter("warning_timeout", WarningTimeout)
            .Default(TDuration::MilliSeconds(1000));

        RegisterInitializer([this] () {
            RefreshTime = TDuration::Seconds(60);
            ExpireAfterSuccessfulUpdateTime = TDuration::Seconds(120);
            ExpireAfterFailedUpdateTime = TDuration::Seconds(30);
        });
    }
};

DEFINE_REFCOUNTED_TYPE(TAddressResolverConfig)

////////////////////////////////////////////////////////////////////////////////

} // namespace NNet
} // namespace NYT
