#include "stdafx.h"
#include "reader_cache.h"
#include "private.h"
#include "config.h"
#include "chunk.h"
#include "location.h"

#include <ytlib/chunk_client/file_reader.h>

#include <core/misc/cache.h>

namespace NYT {
namespace NDataNode {

using namespace NChunkClient;

////////////////////////////////////////////////////////////////////////////////

static auto& Logger = DataNodeLogger;

////////////////////////////////////////////////////////////////////////////////

class TReaderCache::TCachedReader
    : public TCacheValueBase<TChunkId, TCachedReader>
    , public TFileReader
{
public:
    TCachedReader(const TChunkId& chunkId, const Stroka& fileName)
        : TCacheValueBase<TChunkId, TCachedReader>(chunkId)
        , TFileReader(fileName)
        , ChunkId_(chunkId)
    { }

    virtual TChunkId GetChunkId() const override
    {
        return ChunkId_;
    }

private:
    TChunkId ChunkId_;

};

////////////////////////////////////////////////////////////////////////////////

class TReaderCache::TImpl
    : public TSizeLimitedCache<TChunkId, TCachedReader>
{
public:
    explicit TImpl(TDataNodeConfigPtr config)
        : TSizeLimitedCache<TChunkId, TCachedReader>(config->MaxCachedReaders)
    { }

    TGetReaderResult Get(TChunkPtr chunk)
    {
        YCHECK(chunk->IsReadLockAcquired());

        auto location = chunk->GetLocation();
        auto& Profiler = location->Profiler();

        auto chunkId = chunk->GetId();
        TInsertCookie cookie(chunkId);
        if (BeginInsert(&cookie)) {
            auto fileName = chunk->GetFileName();
            LOG_DEBUG("Started opening chunk reader (LocationId: %s, ChunkId: %s)",
                ~location->GetId(),
                ~ToString(chunkId));

            PROFILE_TIMING ("/chunk_reader_open_time") {
                try {
                    auto reader = New<TCachedReader>(chunkId, fileName);
                    reader->Open();
                    cookie.EndInsert(reader);
                } catch (const std::exception& ex) {
                    auto error = TError(
                        NChunkClient::EErrorCode::IOError,
                        "Error opening chunk %s",
                        ~ToString(chunkId))
                        << ex;
                    cookie.Cancel(error);
                    chunk->GetLocation()->Disable();
                    return error;
                }
            }

            LOG_DEBUG("Finished opening chunk reader (LocationId: %s, ChunkId: %s)",
                ~chunk->GetLocation()->GetId(),
                ~ToString(chunkId));
        }

        return cookie.GetValue().Get();
    }

    void Evict(TChunk* chunk)
    {
        TCacheBase::Remove(chunk->GetId());
    }
};

////////////////////////////////////////////////////////////////////////////////

TReaderCache::TReaderCache(TDataNodeConfigPtr config)
    : Impl_(New<TImpl>(config))
{ }

TReaderCache::~TReaderCache()
{ }

TReaderCache::TGetReaderResult TReaderCache::GetReader(TChunkPtr chunk)
{
    return Impl_->Get(chunk);
}

void TReaderCache::EvictReader(TChunk* chunk)
{
    Impl_->Evict(chunk);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NDataNode
} // namespace NYT
