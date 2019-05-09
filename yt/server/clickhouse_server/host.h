#pragma once

#include "query_context.h"

#include "private.h"

#include <yt/core/actions/public.h>

#include <string>

namespace NYT::NClickHouseServer {

////////////////////////////////////////////////////////////////////////////////

class TClickHouseHost
    : public TRefCounted
{
public:
    TClickHouseHost(
        TBootstrap* bootstrap,
        ICoordinationServicePtr coordinationService,
        TClickHouseServerBootstrapConfigPtr nativeConfig,
        std::string cliqueId,
        std::string instanceId,
        ui16 tcpPort,
        ui16 httpPort);

    ~TClickHouseHost();

    void Start();

    //! Change internal user -> query count mapping value, which is used in profiling.
    /*!
     *  \note Invoker affinity: Control invoker
     */
    void AdjustQueryCount(const TString& user, EQueryKind queryKind, int delta);

    const IInvokerPtr& GetControlInvoker() const;

private:
    class TImpl;
    const TIntrusivePtr<TImpl> Impl_;
};

DEFINE_REFCOUNTED_TYPE(TClickHouseHost)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NClickHouseServer
