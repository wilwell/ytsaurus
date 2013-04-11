﻿#pragma once

#include "public.h"

#include <ytlib/table_client/public.h>
#include <ytlib/table_client/table_reader.pb.h>

#include <ytlib/chunk_client/public.h>

#include <ytlib/node_tracker_client/public.h>

#include <ytlib/rpc/public.h>

namespace NYT {
namespace NJobProxy {

////////////////////////////////////////////////////////////////////////////////

NTableClient::ISyncReaderPtr CreateSortingReader(
    NTableClient::TTableReaderConfigPtr config,
    NRpc::IChannelPtr masterChannel,
    NChunkClient::IBlockCachePtr blockCache,
    NNodeTrackerClient::TNodeDirectoryPtr nodeDirectory,
    const NTableClient::TKeyColumns& keyColumns,
    TClosure onNetworkReleased,
    std::vector<NTableClient::NProto::TInputChunk>&& chunks,
    int estimatedRowCount,
    bool isApproximate);

////////////////////////////////////////////////////////////////////////////////

} // namespace NJobProxy
} // namespace NYT
