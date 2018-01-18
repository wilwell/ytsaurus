#include <yt/core/rpc/helpers.h>

namespace NYT {
namespace NFormats {

////////////////////////////////////////////////////////////////////////////////

//! Returns pointer only for side effect of linking static object.
NRpc::IMessageFormat* RegisterJsonRpcMessageFormat();

////////////////////////////////////////////////////////////////////////////////

} // namespace NFormats
} // namespace NYT
