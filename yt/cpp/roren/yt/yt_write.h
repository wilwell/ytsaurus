#pragma once

#include <yt/cpp/roren/yt/transforms.h>

namespace NRoren {

////////////////////////////////////////////////////////////////////////////////

TYtWriteTransform YtWrite(const NYT::TRichYPath& path, const NYT::TTableSchema& schema);

////////////////////////////////////////////////////////////////////////////////

TYtWriteTransform YtSortedWrite(
    const NYT::TRichYPath& path,
    const NYT::TTableSchema& schema,
    const std::vector<std::string>& columnsToSort);

////////////////////////////////////////////////////////////////////////////////

} // namespace NRoren
