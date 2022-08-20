#include "config.h"

namespace NYT::NTracing {

////////////////////////////////////////////////////////////////////////////////

void TTracingConfig::Register(TRegistrar registrar)
{
    registrar.Parameter("send_baggage", &TThis::SendBaggage)
        .Default(true);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTracing
