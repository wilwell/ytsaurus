﻿#pragma once

#include <ytlib/misc/common.h>

namespace NYT {
namespace NCompression {

///////////////////////////////////////////////////////////////////////////////

struct ICodec;

DECLARE_ENUM(ECodec,
    ((None)                       (0))
    ((Snappy)                     (1))
    ((GzipNormal)                 (2))
    ((GzipBestCompression)        (3))
    ((Lz4)                        (4))
    ((Lz4HighCompression)         (5))
    ((QuickLz)                    (6))
);

///////////////////////////////////////////////////////////////////////////////

} // namespace NCompression
} // namespace NYT
