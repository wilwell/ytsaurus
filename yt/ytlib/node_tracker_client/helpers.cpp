#include "helpers.h"

#include <yt/core/ytree/fluent.h>

#include <yt/ytlib/object_client/helpers.h>

#include <limits>

namespace NYT {
namespace NNodeTrackerClient {

using namespace NYson;
using namespace NYTree;
using namespace NObjectClient;
using namespace NNodeTrackerClient::NProto;

////////////////////////////////////////////////////////////////////

Stroka FormatResourceUsage(
    const TNodeResources& usage,
    const TNodeResources& limits)
{
    return Format(
        "UserSlots: %v/%v, Cpu: %v/%v, Memory: %v/%v, Network: %v/%v, "
        "ReplicationSlots: %v/%v, ReplicationDataSize: %v/%v, "
        "RemovalSlots: %v/%v, "
        "RepairSlots: %v/%v, RepairDataSize: %v/%v, "
        "SealSlots: %v/%v",
        // User slots
        usage.user_slots(),
        limits.user_slots(),
        // Cpu
        usage.cpu(),
        limits.cpu(),
        // Memory (in MB)
        usage.memory() / (1024 * 1024),
        limits.memory() / (1024 * 1024),
        // Network
        usage.network(),
        limits.network(),
        // Replication slots
        usage.replication_slots(),
        limits.replication_slots(),
        // Replication data size
        usage.replication_data_size(),
        limits.replication_data_size(),
        // Removal slots
        usage.removal_slots(),
        limits.removal_slots(),
        // Repair slots
        usage.repair_slots(),
        limits.repair_slots(),
        // Repair data size
        usage.repair_data_size(),
        limits.repair_data_size(),
        // Seal slots
        usage.seal_slots(),
        limits.seal_slots());
}

Stroka FormatResources(const TNodeResources& resources)
{
    return Format(
        "UserSlots: %v, Cpu: %v, Memory: %v, Network: %v, "
        "ReplicationSlots: %v, ReplicationDataSize: %v, "
        "RemovalSlots: %v, "
        "RepairSlots: %v, RepairDataSize: %v, "
        "SealSlots: %v",
        resources.user_slots(),
        resources.cpu(),
        resources.memory() / (1024 * 1024),
        resources.network(),
        resources.replication_slots(),
        resources.replication_data_size() / (1024 * 1024),
        resources.removal_slots(),
        resources.repair_slots(),
        resources.repair_data_size() / (1024 * 1024),
        resources.seal_slots());
}

void ProfileResources(NProfiling::TProfiler& profiler, const TNodeResources& resources)
{
    #define XX(name, Name) profiler.Enqueue("/" #name, resources.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
}

EResourceType GetDominantResource(
    const TNodeResources& demand,
    const TNodeResources& limits)
{
    auto maxType = EResourceType::Cpu;
    double maxRatio = 0.0;
    auto update = [&] (i64 a, i64 b, EResourceType type) {
        if (b > 0) {
            double ratio = (double) a / b;
            if (ratio > maxRatio) {
                maxRatio = ratio;
                maxType = type;
            }
        }
    };
    update(demand.user_slots(), limits.user_slots(), EResourceType::UserSlots);    
    update(demand.cpu(), limits.cpu(), EResourceType::Cpu);
    update(demand.memory(), limits.memory(), EResourceType::Memory);
    update(demand.network(), limits.network(), EResourceType::Network);
    return maxType;
}

i64 GetResource(const TNodeResources& resources, EResourceType type)
{
    switch (type) {
        #define XX(name, Name) case EResourceType::Name: return resources.name();
        ITERATE_NODE_RESOURCES(XX)
        #undef XX
        default:
            YUNREACHABLE();
    }
}

void SetResource(TNodeResources& resources, EResourceType type, i64 value)
{
    switch (type) {
        #define XX(name, Name) \
            case EResourceType::Name: \
            resources.set_##name(static_cast<decltype(resources.name())>(value)); break;
        ITERATE_NODE_RESOURCES(XX)
        #undef XX
        default:
            YUNREACHABLE();
    }
}

double GetMinResourceRatio(
    const TNodeResources& nominator,
    const TNodeResources& denominator)
{
    double result = std::numeric_limits<double>::infinity();
    auto update = [&] (i64 a, i64 b) {
        if (b > 0) {
            result = std::min(result, (double) a / b);
        }
    };
    update(nominator.user_slots(), denominator.user_slots());
    update(nominator.cpu(), denominator.cpu());
    update(nominator.memory(), denominator.memory());
    update(nominator.network(), denominator.network());
    return result;
}

TNodeResources GetAdjustedResourceLimits(
    const TNodeResources& demand,
    const TNodeResources& limits,
    int nodeCount)
{
    auto adjustedLimits = limits;

    // Take memory granularity into account.
    if (demand.user_slots() > 0 && nodeCount > 0) {
        i64 memoryDemandPerJob = demand.memory() / demand.user_slots();
        i64 memoryLimitPerNode = limits.memory() / nodeCount;
        int slotsPerNode = memoryLimitPerNode / memoryDemandPerJob;
        i64 adjustedMemoryLimit = slotsPerNode * memoryDemandPerJob * nodeCount;
        adjustedLimits.set_memory(adjustedMemoryLimit);
    }

    return adjustedLimits;
}

TNodeResources GetZeroNodeResources()
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(0);
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

const TNodeResources& ZeroNodeResources()
{
    static auto value = GetZeroNodeResources();
    return value;
}

TNodeResources GetInfiniteResources()
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(std::numeric_limits<decltype(result.name())>::max() / 4);
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

const TNodeResources& InfiniteNodeResources()
{
    static auto result = GetInfiniteResources();
    return result;
}

TObjectId ObjectIdFromNodeId(TNodeId nodeId, TCellTag cellTag)
{
    return MakeId(EObjectType::ClusterNode, cellTag, nodeId, 0);
}

TNodeId NodeIdFromObjectId(const TObjectId& objectId)
{
    return CounterFromId(objectId);
}

namespace NProto {

TNodeResources operator + (const TNodeResources& lhs, const TNodeResources& rhs)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(lhs.name() + rhs.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

TNodeResources& operator += (TNodeResources& lhs, const TNodeResources& rhs)
{
    #define XX(name, Name) lhs.set_##name(lhs.name() + rhs.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return lhs;
}

TNodeResources operator - (const TNodeResources& lhs, const TNodeResources& rhs)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(lhs.name() - rhs.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

TNodeResources& operator -= (TNodeResources& lhs, const TNodeResources& rhs)
{
    #define XX(name, Name) lhs.set_##name(lhs.name() - rhs.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return lhs;
}

TNodeResources operator * (const TNodeResources& lhs, i64 rhs)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(lhs.name() * rhs);
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

TNodeResources operator * (const TNodeResources& lhs, double rhs)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(static_cast<decltype(lhs.name())>(lhs.name() * rhs + 0.5));
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

TNodeResources& operator *= (TNodeResources& lhs, i64 rhs)
{
    #define XX(name, Name) lhs.set_##name(lhs.name() * rhs);
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return lhs;
}

TNodeResources& operator *= (TNodeResources& lhs, double rhs)
{
    #define XX(name, Name) lhs.set_##name(static_cast<decltype(lhs.name())>(lhs.name() * rhs + 0.5));
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return lhs;
}

TNodeResources  operator - (const TNodeResources& resources)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(-resources.name());
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

bool operator == (const TNodeResources& lhs, const TNodeResources& rhs)
{
    return
        #define XX(name, Name) lhs.name() == rhs.name() &&
        ITERATE_NODE_RESOURCES(XX)
        #undef XX
        true;
}

bool operator != (const TNodeResources& lhs, const TNodeResources& rhs)
{
    return !(lhs == rhs);
}

TNodeResources MakeNonnegative(const TNodeResources& resources)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(std::max(resources.name(), static_cast<decltype(resources.name())>(0)));
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

bool Dominates(const TNodeResources& lhs, const TNodeResources& rhs)
{
    return
        #define XX(name, Name) lhs.name() >= rhs.name() &&
        ITERATE_NODE_RESOURCES(XX)
        #undef XX
        true;
}

bool DominatesNonnegative(const TNodeResources& lhs, const TNodeResources& rhs)
{
    auto nonnegLhs = MakeNonnegative(lhs);
    auto nonnegRhs = MakeNonnegative(rhs);
    return Dominates(nonnegLhs, nonnegRhs);
}

TNodeResources Max(const TNodeResources& a, const TNodeResources& b)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(std::max(a.name(), b.name()));
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

TNodeResources Min(const TNodeResources& a, const TNodeResources& b)
{
    TNodeResources result;
    #define XX(name, Name) result.set_##name(std::min(a.name(), b.name()));
    ITERATE_NODE_RESOURCES(XX)
    #undef XX
    return result;
}

void Serialize(const TNodeResources& resources, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginMap()
            #define XX(name, Name) .Item(#name).Value(resources.name())
            ITERATE_NODE_RESOURCES(XX)
            #undef XX
        .EndMap();
}

} // namespace NProto

////////////////////////////////////////////////////////////////////

} // namespace NNodeTrackerClient
} // namespace NYT

