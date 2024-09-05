#ifndef AUTH_CACHE_INL_H_
#error "Direct inclusion of this file is not allowed, include auth_cache-inl.h"
// For the sake of sane code completion.
#include "auth_cache.h"
#endif

#include "config.h"
#include "private.h"

#include <yt/yt/core/profiling/timing.h>

namespace NYT::NAuth {

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue>
TAuthCache<TKey, TValue>::TEntry::TEntry(const TKey& key)
    : Key(key)
    , LastAccessTime(GetCpuInstant())
    , LastUpdateTime(GetCpuInstant())
{ }

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue>
TAuthCache<TKey, TValue>::TAuthCache(
    TAuthCacheConfigPtr config,
    NProfiling::TProfiler profiler)
    : Config_(std::move(config))
    , Profiler_(std::move(profiler))
{ }

template <class TKey, class TValue>
TFuture<TValue> TAuthCache<TKey, TValue>::Get(const TKey& key)
{
    TEntryPtr entry;
    {
        auto guard = ReaderGuard(SpinLock_);
        auto it = Cache_.find(key);
        if (it != Cache_.end()) {
            entry = it->second;
        }
    }

    auto now = NProfiling::GetCpuInstant();

    if (entry) {
        auto guard = Guard(entry->Lock);
        auto future = entry->Future;

        entry->LastAccessTime = now;

        if (entry->IsOutdated(Config_->CacheTtl, Config_->ErrorTtl) && !entry->Updating) {
            entry->LastUpdateTime = now;
            entry->Updating = true;

            guard.Release();

            DoGet(entry->Key)
                .Subscribe(BIND([entry] (const TErrorOr<TValue>& value) {
                    auto transientError = !value.IsOK() && !value.FindMatching(NRpc::EErrorCode::InvalidCredentials);

                    auto guard = Guard(entry->Lock);
                    entry->Updating = false;

                    if (transientError) {
                        const auto& Logger = AuthLogger;
                        YT_LOG_DEBUG(value, "Skipping transient error while updating authentication cache entry");
                        return;
                    }

                    entry->Future = MakeFuture(value);
                }));
        }

        return future;
    }

    entry = New<TEntry>(key);
    entry->Promise = NewPromise<TValue>();
    entry->Future = entry->Promise.ToFuture();
    entry->LastUpdateTime = now;

    bool inserted = false;

    {
        auto writerGuard = WriterGuard(SpinLock_);
        auto it = Cache_.find(key);
        if (it == Cache_.end()) {
            inserted = true;
            Cache_[key] = entry;
        } else {
            entry = it->second;
        }
    }

    if (inserted) {
        entry->EraseCookie = NConcurrency::TDelayedExecutor::Submit(
            BIND(&TAuthCache::TryErase, MakeWeak(this), MakeWeak(entry)),
            Config_->OptimisticCacheTtl);

        entry->Promise.SetFrom(DoGet(entry->Key).ToUncancelable());
    }

    auto guard = Guard(entry->Lock);
    return entry->Future;
}

template <class TKey, class TValue>
void TAuthCache<TKey, TValue>::TryErase(const TWeakPtr<TEntry>& weakEntry)
{
    auto entry = weakEntry.Lock();
    if (!entry) {
        return;
    }

    auto guard = Guard(entry->Lock);
    if (entry->IsExpired(Config_->OptimisticCacheTtl)) {
        auto writerGuard = WriterGuard(SpinLock_);
        auto it = Cache_.find(entry->Key);
        if (it != Cache_.end() && it->second == entry) {
            Cache_.erase(it);
        }
    } else {
        entry->EraseCookie = NConcurrency::TDelayedExecutor::Submit(
            BIND(&TAuthCache::TryErase, MakeWeak(this), MakeWeak(entry)),
            Config_->OptimisticCacheTtl);
    }
}

////////////////////////////////////////////////////////////////////////////////

template <class TKey, class TValue>
bool TAuthCache<TKey, TValue>::TEntry::IsOutdated(TDuration ttl, TDuration errorTtl)
{
    auto now = NProfiling::GetCpuInstant();

    auto value = Future.TryGet();
    if (value && !value->IsOK()) {
        return now > LastUpdateTime + NProfiling::DurationToCpuDuration(errorTtl);
    } else {
        return now > LastUpdateTime + NProfiling::DurationToCpuDuration(ttl);
    }
}

template<class TKey, class TValue>
bool TAuthCache<TKey, TValue>::TEntry::IsExpired(TDuration ttl)
{
    auto now = NProfiling::GetCpuInstant();
    return now > LastAccessTime + NProfiling::DurationToCpuDuration(ttl);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NAuth
