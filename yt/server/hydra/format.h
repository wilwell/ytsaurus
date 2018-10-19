#pragma once

#include "public.h"

#include <yt/core/compression/public.h>

#include <yt/core/misc/serialize.h>

#include <util/system/align.h>

namespace NYT {
namespace NHydra {

////////////////////////////////////////////////////////////////////////////////

#pragma pack(push, 1)

struct TChangelogHeader
{
    //! Used for format validation.
    static const ui64 ExpectedSignature = 0x3430303044435459ull; // YTCD0004

    //! Indicates that the changelog is not yet sealed.
    static const i32 NotTruncatedRecordCount = -2;

    ui64 Signature;
    i32 HeaderSize; // with padding
    i32 MetaSize;
    i32 TruncatedRecordCount;
    i32 PaddingSize;

    TChangelogHeader()
        : Signature(-1)
        , HeaderSize(-1)
        , MetaSize(-1)
        , PaddingSize(-1)
    { }

    TChangelogHeader(
        int metaSize,
        int sealedRecordCount,
        int alignment)
        : Signature(ExpectedSignature)
        , HeaderSize(::AlignUp<size_t>(sizeof(TChangelogHeader) + metaSize, alignment))
        , MetaSize(metaSize)
        , TruncatedRecordCount(sealedRecordCount)
        , PaddingSize(HeaderSize - sizeof(TChangelogHeader) - metaSize)
    { }
};

static_assert(sizeof (TChangelogHeader) == 24, "Binary size of TChangelogHeader has changed.");

////////////////////////////////////////////////////////////////////////////////

struct TChangelogRecordHeader
{
    i32 RecordId;
    i32 DataSize;
    TChecksum Checksum;
    i32 PaddingSize;

    TChangelogRecordHeader()
        : RecordId(-1)
        , DataSize(-1)
        , Checksum(0)
        , PaddingSize(-1)
    { }

    TChangelogRecordHeader(
        int recordIndex,
        int dataLength,
        TChecksum checksum,
        int paddingSize)
        : RecordId(recordIndex)
        , DataSize(dataLength)
        , Checksum(checksum)
        , PaddingSize(paddingSize)
    { }
};

static_assert(sizeof(TChangelogRecordHeader) == 20, "Binary size of TChangelogRecordHeader has changed.");

////////////////////////////////////////////////////////////////////////////////

struct TChangelogIndexHeader
{
    //! Used for format validation.
    static const ui64 ExpectedSignature = 0x3430303049435459ull; // YTCI0004

    ui64 Signature;
    i32 IndexRecordCount;
    ui32 Padding = 0;

    TChangelogIndexHeader()
        : Signature(0)
        , IndexRecordCount(-1)
    { }

    explicit TChangelogIndexHeader(int indexRecordCount)
        : Signature(ExpectedSignature)
        , IndexRecordCount(indexRecordCount)
    { }
};

static_assert(sizeof(TChangelogIndexHeader) == 16, "Binary size of TChangelogIndexHeader has changed.");

////////////////////////////////////////////////////////////////////////////////

struct TChangelogIndexRecord
{
    i64 FilePosition;
    i32 RecordId;
    ui32 Padding = 0;

    //! This initializer is only needed to read TLogIndexRecord.
    TChangelogIndexRecord()
        : FilePosition(-1)
        , RecordId(-1)
    { }

    TChangelogIndexRecord(int recordIndex, i64 filePosition)
        : FilePosition(filePosition)
        , RecordId(recordIndex)
    { }
};

static_assert(sizeof(TChangelogIndexRecord) == 16, "Binary size of TLogIndexRecord has changed.");

////////////////////////////////////////////////////////////////////////////////

struct TSnapshotHeader
{
    static const ui64 ExpectedSignature = 0x3330303053535459ull; // YTSS0003

    ui64 Signature = ExpectedSignature;
    i32 SnapshotId = 0;
    ui64 CompressedLength = 0;
    ui64 UncompressedLength = 0;
    ui64 Checksum = 0;
    NCompression::ECodec Codec = NCompression::ECodec::None;
    ui8 Padding[3] = {};
    i32 MetaSize = 0;
};

static_assert(sizeof(TSnapshotHeader) == 44, "Binary size of TSnapshotHeader has changed.");

#pragma pack(pop)

////////////////////////////////////////////////////////////////////////////////

} // namespace NHydra
} // namespace NYT

Y_DECLARE_PODTYPE(NYT::NHydra::TChangelogHeader);
Y_DECLARE_PODTYPE(NYT::NHydra::TChangelogRecordHeader);
Y_DECLARE_PODTYPE(NYT::NHydra::TChangelogIndexHeader);
Y_DECLARE_PODTYPE(NYT::NHydra::TChangelogIndexRecord);
Y_DECLARE_PODTYPE(NYT::NHydra::TSnapshotHeader);

