#include "serialize.h"

#include <util/generic/cast.h>

namespace NYT::NControllerAgent {

////////////////////////////////////////////////////////////////////////////////

int GetCurrentSnapshotVersion()
{
    return ToUnderlying(TEnumTraits<ESnapshotVersion>::GetDomainValues().back());
}

bool ValidateSnapshotVersion(int version)
{
    return version >= ToUnderlying(ESnapshotVersion::ForceAdvanceBefore19_6) && version <= GetCurrentSnapshotVersion();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
