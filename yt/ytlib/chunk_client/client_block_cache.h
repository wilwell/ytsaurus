#pragma once

#include "public.h"
#include "block_cache.h"

#include <ytlib/misc/configurable.h>

namespace NYT {
namespace NChunkClient {

///////////////////////////////////////////////////////////////////////////////

//! Creates a simple client-side block cache.
IBlockCachePtr CreateClientBlockCache(TClientBlockCacheConfigPtr config);

///////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT
