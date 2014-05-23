#include "stdafx.h"
#include "chunk_detail.h"
#include "private.h"
#include "location.h"

#include <server/cell_node/bootstrap.h>

namespace NYT {
namespace NDataNode {

using namespace NCellNode;
using namespace NChunkClient;
using namespace NChunkClient::NProto;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

TChunk::TChunk(
    TLocationPtr location,
    const TChunkId& id,
    const TChunkInfo& info,
    TNodeMemoryTracker* memoryUsageTracker)
    : Id_(id)
    , Location_(location)
    , Info_(info)
    , MemoryUsageTracker_(memoryUsageTracker)
{ }

TChunk::TChunk(
    TLocationPtr location,
    const TChunkDescriptor& descriptor,
    TNodeMemoryTracker* memoryUsageTracker)
    : Id_(descriptor.Id)
    , Location_(location)
    , MemoryUsageTracker_(memoryUsageTracker)
{
    Info_.set_disk_space(descriptor.DiskSpace);
    Info_.clear_meta_checksum();
}

TChunk::~TChunk()
{
    auto cachedMeta = GetCachedMeta();
    if (cachedMeta) {
        MemoryUsageTracker_->Release(EMemoryConsumer::ChunkMeta, cachedMeta->SpaceUsed());
    }
}

const TChunkId& TChunk::GetId() const
{
    return Id_;
}

TLocationPtr TChunk::GetLocation() const
{
    return Location_;
}

const TChunkInfo& TChunk::GetInfo() const
{
    return Info_;
}

Stroka TChunk::GetFileName() const
{
    return Location_->GetChunkFileName(Id_);
}

bool TChunk::TryAcquireReadLock()
{
    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedEvent_) {
            LOG_DEBUG("Chunk read lock cannot be acquired since removal is already pending (ChunkId: %s)",
                ~ToString(Id_));
            return false;
        }

        lockCount = ++ReadLockCounter_;
    }

    LOG_DEBUG("Chunk read lock acquired (ChunkId: %s, LockCount: %d)",
        ~ToString(Id_),
        lockCount);

    return true;
}

void TChunk::ReleaseReadLock()
{
    bool scheduleRemoval = false;
    int lockCount;
    {
        TGuard<TSpinLock> guard(SpinLock_);
        YCHECK(ReadLockCounter_ > 0);
        lockCount = --ReadLockCounter_;
        if (ReadLockCounter_ == 0 && !RemovalScheduled_ && RemovedEvent_) {
            scheduleRemoval = RemovalScheduled_ = true;
        }
    }

    LOG_DEBUG("Chunk read lock released (ChunkId: %s, LockCount: %d)",
        ~ToString(Id_),
        lockCount);

    if (scheduleRemoval) {
        DoRemove();
    }
}

bool TChunk::IsReadLockAcquired() const
{
    return ReadLockCounter_ > 0;
}

TFuture<void> TChunk::ScheduleRemoval()
{
    bool scheduleRemoval = false;

    {
        TGuard<TSpinLock> guard(SpinLock_);
        if (RemovedEvent_) {
            return RemovedEvent_;
        }

        RemovedEvent_ = NewPromise();
        if (ReadLockCounter_ == 0 && !RemovalScheduled_) {
            scheduleRemoval = RemovalScheduled_ = true;
        }
    }

    if (scheduleRemoval) {
        DoRemove();
    }

    return RemovedEvent_;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
