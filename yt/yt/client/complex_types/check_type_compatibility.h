#include <yt/client/table_client/public.h>

#include <yt/core/misc/enum.h>

namespace NYT::NComplexTypes {

////////////////////////////////////////////////////////////////////////////////

// Returned value is pair with elements
//   1. Compatibility of types.
//   2. If types are NOT FullyCompatible, error contains description of incompatibility.
std::pair<NTableClient::ESchemaCompatibility, TError> CheckTypeCompatibility(
    const NYT::NTableClient::TLogicalTypePtr& oldType,
    const NYT::NTableClient::TLogicalTypePtr& newType);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NComplexTypes
