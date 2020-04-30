#include "client_method_options.h"

namespace NYT {

template <typename T>
static void MergeMaybe(TMaybe<T>& origin, const TMaybe<T>& patch)
{
    if (patch) {
        origin = patch;
    }
}

void TFormatHints::Merge(const TFormatHints& patch)
{
    if (patch.SkipNullValuesForTNode_) {
        SkipNullValuesForTNode(true);
    }
    MergeMaybe(EnableStringToAllConversion_, patch.EnableStringToAllConversion_);
    MergeMaybe(EnableAllToStringConversion_, patch.EnableAllToStringConversion_);
    MergeMaybe(EnableIntegralTypeConversion_, patch.EnableIntegralTypeConversion_);
    MergeMaybe(EnableIntegralToDoubleConversion_, patch.EnableIntegralToDoubleConversion_);
    MergeMaybe(EnableTypeConversion_, patch.EnableTypeConversion_);
}

} // namespace NYT
