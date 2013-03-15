﻿#pragma once

#include "public.h"
#include "value.h"

#include <ytlib/table_client/table_reader.pb.h>
#include <ytlib/chunk_client/async_reader.h>
#include <ytlib/chunk_client/public.h>
#include <ytlib/logging/tagged_logger.h>
#include <ytlib/misc/async_stream_state.h>
#include <ytlib/compression/codec.h>

namespace NYT {
namespace NTableClient {

////////////////////////////////////////////////////////////////////////////////

class TPartitionChunkReaderProvider
    : public TRefCounted
{
public:
    TPartitionChunkReaderProvider(const NChunkClient::TSequentialReaderConfigPtr& config);

    TPartitionChunkReaderPtr CreateNewReader(
        const NProto::TInputChunk& inputChunk,
        const NChunkClient::IAsyncReaderPtr& chunkReader);

    bool KeepInMemory() const;

private:
    NChunkClient::TSequentialReaderConfigPtr Config;

};

////////////////////////////////////////////////////////////////////////////////

class TPartitionChunkReader
    : public virtual TRefCounted
{
    DEFINE_BYVAL_RO_PROPERTY(const char*, RowPointer);
    DEFINE_BYVAL_RO_PROPERTY(i64, RowCount);

public:
    typedef TPartitionChunkReaderProvider TProvider;

    TPartitionChunkReader(
        const NChunkClient::TSequentialReaderConfigPtr& sequentialReader,
        const NChunkClient::IAsyncReaderPtr& asyncReader,
        int partitionTag,
        NCompression::ECodec codecId);

    TAsyncError AsyncOpen();

    bool IsValid() const;

    bool FetchNextItem();
    TAsyncError GetReadyEvent();

    TValue ReadValue(const TStringBuf& name);

    i64 GetRowIndex() const;

    const NYTree::TYsonString& GetRowAttributes() const;

    //! Must be called after AsyncOpen has finished.
    TFuture<void> GetFetchingCompleteEvent();

private:
    NChunkClient::TSequentialReaderConfigPtr SequentialConfig;
    NChunkClient::IAsyncReaderPtr AsyncReader;

    i64 CurrentRowIndex;
    int PartitionTag;
    NCompression::ECodec CodecId;

    TAsyncStreamState State;
    NChunkClient::TSequentialReaderPtr SequentialReader;

    std::vector<TSharedRef> Blocks;
    yhash_map<TStringBuf, TValue> CurrentRow;

    ui64 SizeToNextRow;

    TMemoryInput DataBuffer;
    TMemoryInput SizeBuffer;

    NLog::TTaggedLogger Logger;

    void OnGotMeta(NChunkClient::IAsyncReader::TGetMetaResult result);
    void OnNextBlock(TError error);

    bool NextRow();

    void OnFail(const TError& error);

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableClient
} // namespace NYT

