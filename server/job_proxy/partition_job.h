#pragma once

#include "public.h"

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

IJobPtr CreatePartitionJob(IJobHostPtr host);

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
