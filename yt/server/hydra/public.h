#pragma once

#include <core/misc/common.h>
#include <core/misc/lazy_ptr.h>

#include <core/concurrency/action_queue.h>

#include <ytlib/hydra/public.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

DECLARE_REFCOUNTED_STRUCT(IAutomaton)
DECLARE_REFCOUNTED_STRUCT(IHydraManager)

struct TChangelogCreateParams;
DECLARE_REFCOUNTED_STRUCT(IChangelog)
DECLARE_REFCOUNTED_STRUCT(IChangelogStore)
DECLARE_REFCOUNTED_STRUCT(IChangelogCatalog)

struct TSnapshotParams;
struct TSnapshotCreateParams;
DECLARE_REFCOUNTED_STRUCT(ISnapshotReader)
DECLARE_REFCOUNTED_STRUCT(ISnapshotWriter)
DECLARE_REFCOUNTED_STRUCT(ISnapshotStore)
DECLARE_REFCOUNTED_CLASS(TFileSnapshotStore)

struct TMutationRequest;
struct TMutationResponse;
class TMutationContext; 

DECLARE_REFCOUNTED_CLASS(TCompositeAutomaton)
DECLARE_REFCOUNTED_CLASS(TCompositeAutomatonPart)

DECLARE_REFCOUNTED_CLASS(TMutation)

class TSaveContext;
class TLoadContext;

DECLARE_REFCOUNTED_CLASS(TFileChangelogConfig)
DECLARE_REFCOUNTED_CLASS(TFileChangelogStoreConfig)
DECLARE_REFCOUNTED_CLASS(TMultiplexedFileChangelogConfig)
DECLARE_REFCOUNTED_CLASS(TFileChangelogCatalogConfig)
DECLARE_REFCOUNTED_CLASS(TLocalSnapshotStoreConfig)
DECLARE_REFCOUNTED_CLASS(TRemoteSnapshotStoreConfig)
DECLARE_REFCOUNTED_CLASS(TSnapshotDownloaderConfig)
DECLARE_REFCOUNTED_CLASS(TChangelogDownloaderConfig)
DECLARE_REFCOUNTED_CLASS(TFollowerTrackerConfig)
DECLARE_REFCOUNTED_CLASS(TLeaderCommitterConfig)
DECLARE_REFCOUNTED_CLASS(TDistributedHydraManagerConfig)

////////////////////////////////////////////////////////////////////////////////

extern TLazyIntrusivePtr<NConcurrency::TActionQueue> HydraIOQueue;

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT
