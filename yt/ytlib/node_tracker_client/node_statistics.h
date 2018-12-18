#pragma once

#include "public.h"

#include <yt/ytlib/chunk_client/public.h>

#include <yt/client/node_tracker_client/proto/node.pb.h>

#include <array>

namespace NYT::NNodeTrackerClient {

////////////////////////////////////////////////////////////////////////////////

struct TDiskSpaceStatistics
{
    i64 Available = 0;
    i64 Used = 0;
};

struct TTotalNodeStatistics
{
    TDiskSpaceStatistics TotalSpace;
    std::array<TDiskSpaceStatistics, NChunkClient::MaxMediumCount> SpacePerMedium = {};

    i64 ChunkReplicaCount = 0;

    int OnlineNodeCount = 0;
    int OfflineNodeCount = 0;
    int BannedNodeCount = 0;
    int DecommissinedNodeCount = 0;
    int WithAlertsNodeCount = 0;
    int FullNodeCount = 0;
};

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

TString ToString(const TNodeStatistics& statistics);

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerClient
