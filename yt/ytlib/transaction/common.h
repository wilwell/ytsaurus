#pragma once

#include "../misc/common.h"
#include "../misc/ptr.h"

#include "../logging/log.h"

namespace NYT {
namespace NTransaction {

////////////////////////////////////////////////////////////////////////////////

extern NLog::TLogger TransactionLogger;

////////////////////////////////////////////////////////////////////////////////

typedef TGUID TTransactionId;

////////////////////////////////////////////////////////////////////////////////

} // namespace NTransaction
} // namespace NYT

