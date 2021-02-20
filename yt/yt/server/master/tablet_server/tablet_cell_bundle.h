#pragma once

#include "public.h"
#include "tablet_resources.h"

#include <yt/server/master/object_server/object.h>

#include <yt/server/master/security_server/acl.h>

#include <yt/server/master/cell_master/public.h>
#include <yt/server/master/cell_master/serialize.h>

#include <yt/server/master/cell_server/cell_bundle.h>

#include <yt/ytlib/tablet_client/public.h>

#include <yt/core/misc/ref_tracked.h>
#include <yt/core/misc/arithmetic_formula.h>

#include <yt/core/profiling/public.h>

namespace NYT::NTabletServer {

////////////////////////////////////////////////////////////////////////////////

struct TTabletCellBundleProfilingCounters
{
    TTabletCellBundleProfilingCounters(TString bundleName);

    NProfiling::TGauge TabletCountLimit;
    NProfiling::TGauge TabletCountUsage;
    NProfiling::TGauge TabletStaticMemoryLimit;
    NProfiling::TGauge TabletStaticMemoryUsage;

    TString BundleName;
};

////////////////////////////////////////////////////////////////////////////////

class TTabletCellBundle
    : public NCellServer::TCellBundle
{
public:
    DEFINE_BYREF_RW_PROPERTY(TTabletBalancerConfigPtr, TabletBalancerConfig);

    DEFINE_BYREF_RW_PROPERTY(THashSet<TTabletAction*>, TabletActions);
    DEFINE_BYVAL_RO_PROPERTY(int, ActiveTabletActionCount);

    DEFINE_BYREF_RW_PROPERTY(TTabletResources, ResourceLimits);
    DEFINE_BYREF_RW_PROPERTY(TGossipTabletResources, ResourceUsage);

public:
    explicit TTabletCellBundle(TTabletCellBundleId id);

    void IncreaseActiveTabletActionCount();
    void DecreaseActiveTabletActionCount();

    std::vector<const TTabletCell*> GetAliveCells() const;

    void ValidateResourceUsageIncrease(const TTabletResources& delta) const;
    void UpdateResourceUsage(TTabletResources delta);
    void RecomputeClusterResourceUsage();

    virtual TString GetLowercaseObjectName() const override;
    virtual TString GetCapitalizedObjectName() const override;

    virtual void Save(NCellMaster::TSaveContext& context) const override;
    virtual void Load(NCellMaster::TLoadContext& context) override;

    void OnProfiling(TTabletCellBundleProfilingCounters* counters);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTabletServer
