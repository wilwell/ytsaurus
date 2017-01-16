#include "input_data_slice.h"
#include "chunk_spec.h"

#include <yt/core/misc/protobuf_helpers.h>

#include <yt/ytlib/table_client/serialize.h>

namespace NYT {
namespace NChunkClient {

using namespace NTableClient;

////////////////////////////////////////////////////////////////////////////////

TInputDataSlice::TInputDataSlice(
    EDataSliceDescriptorType type,
    TChunkSliceList chunkSlices,
    TInputSliceLimit lowerLimit,
    TInputSliceLimit upperLimit)
    : LowerLimit_(std::move(lowerLimit))
    , UpperLimit_(std::move(upperLimit))
    , ChunkSlices(std::move(chunkSlices))
    , Type(type)
{ }

int TInputDataSlice::GetChunkCount() const
{
    return ChunkSlices.size();
}

i64 TInputDataSlice::GetDataSize() const
{
    i64 result = 0;
    for (const auto& chunkSlice : ChunkSlices) {
        result += chunkSlice->GetDataSize();
    }
    return result;
}

i64 TInputDataSlice::GetRowCount() const
{
    i64 result = 0;
    for (const auto& chunkSlice : ChunkSlices) {
        result += chunkSlice->GetRowCount();
    }
    return result;
}

i64 TInputDataSlice::GetMaxBlockSize() const
{
    i64 result = 0;
    for (const auto& chunkSlice : ChunkSlices) {
        result = std::max(result, chunkSlice->GetMaxBlockSize());
    }
    return result;
}

void TInputDataSlice::Persist(NTableClient::TPersistenceContext& context)
{
    using NYT::Persist;
    Persist(context, LowerLimit_);
    Persist(context, UpperLimit_);
    Persist(context, ChunkSlices);
    Persist(context, Type);
}

int TInputDataSlice::GetTableIndex() const
{
    YCHECK(ChunkSlices.size() > 0);
    return ChunkSlices[0]->GetInputChunk()->GetTableIndex();
}

TInputChunkPtr TInputDataSlice::GetSingleUnversionedChunkOrThrow() const
{
    if (!IsTrivial()) {
        THROW_ERROR_EXCEPTION("Dynamic table cannot be used in this context");
    }
    return ChunkSlices[0]->GetInputChunk();
}

bool TInputDataSlice::IsTrivial() const
{
    return Type == EDataSliceDescriptorType::UnversionedTable && ChunkSlices.size() == 1;
}

////////////////////////////////////////////////////////////////////////////////

Stroka ToString(const TInputDataSlicePtr& dataSlice)
{
    return Format("Type: %v, LowerLimit: %v, UpperLimit: %v, ChunkSlices: %v",
        dataSlice->Type,
        dataSlice->LowerLimit(),
        dataSlice->UpperLimit(),
        dataSlice->ChunkSlices);
}

void ToProto(
    NProto::TDataSliceDescriptor* dataSliceDescriptor,
    TInputDataSlicePtr inputDataSlice,
    const TTableSchema& schema,
    TTimestamp timestamp)
{
    std::vector<NProto::TChunkSpec> chunkSpecs;

    for (const auto& slice : inputDataSlice->ChunkSlices) {
        NProto::TChunkSpec spec;
        ToProto(&spec, slice);
        chunkSpecs.push_back(std::move(spec));
    }

    TDataSliceDescriptor descriptor(inputDataSlice->Type, std::move(chunkSpecs));
    descriptor.Schema = schema;
    descriptor.Timestamp = timestamp;

    ToProto(dataSliceDescriptor, descriptor);
}

////////////////////////////////////////////////////////////////////////////////

TInputDataSlicePtr CreateUnversionedInputDataSlice(TInputChunkSlicePtr chunkSlice)
{
    return New<TInputDataSlice>(
        EDataSliceDescriptorType::UnversionedTable,
        TInputDataSlice::TChunkSliceList{chunkSlice},
        chunkSlice->LowerLimit(),
        chunkSlice->UpperLimit());
}

TInputDataSlicePtr CreateVersionedInputDataSlice(const std::vector<TInputChunkSlicePtr>& inputChunks)
{
    std::vector<TInputDataSlicePtr> dataSlices;

    YCHECK(!inputChunks.empty());
    TInputDataSlice::TChunkSliceList chunkSlices;
    TNullable<int> tableIndex;
    TInputSliceLimit lowerLimit;
    TInputSliceLimit upperLimit;
    for (const auto& inputChunk : inputChunks) {
        if (!tableIndex) {
            tableIndex = inputChunk->GetInputChunk()->GetTableIndex();
            lowerLimit.Key = inputChunk->LowerLimit().Key;
            upperLimit.Key = inputChunk->UpperLimit().Key;
        } else {
            YCHECK(*tableIndex == inputChunk->GetInputChunk()->GetTableIndex());
            YCHECK(lowerLimit.Key == inputChunk->LowerLimit().Key);
            YCHECK(upperLimit.Key == inputChunk->UpperLimit().Key);
        }
        chunkSlices.push_back(inputChunk);
    }
    return New<TInputDataSlice>(
        EDataSliceDescriptorType::VersionedTable,
        std::move(chunkSlices),
        std::move(lowerLimit),
        std::move(upperLimit));
}

TInputDataSlicePtr CreateInputDataSlice(
    EDataSliceDescriptorType type,
    const std::vector<TInputChunkSlicePtr>& inputChunks,
    TKey lowerKey,
    TKey upperKey)
{
    TInputDataSlice::TChunkSliceList chunkSlices;
    TNullable<int> tableIndex;
    for (const auto& inputChunk : inputChunks) {
        if (!tableIndex) {
            tableIndex = inputChunk->GetInputChunk()->GetTableIndex();
        } else {
            YCHECK(*tableIndex == inputChunk->GetInputChunk()->GetTableIndex());
        }
        chunkSlices.push_back(CreateInputChunkSlice(*inputChunk, lowerKey, upperKey));
    }

    TInputSliceLimit lowerLimit;
    lowerLimit.Key = lowerKey;

    TInputSliceLimit upperLimit;
    upperLimit.Key = upperKey;

    return New<TInputDataSlice>(
        type,
        std::move(chunkSlices),
        std::move(lowerLimit),
        std::move(upperLimit));
}

TInputDataSlicePtr CreateInputDataSlice(
    const TInputDataSlicePtr& dataSlice,
    TKey lowerKey,
    TKey upperKey)
{
    auto lowerLimit = dataSlice->LowerLimit();
    auto upperLimit = dataSlice->UpperLimit();

    if (lowerKey) {
        lowerLimit.MergeLowerKey(lowerKey);
    }

    if (upperKey) {
        upperLimit.MergeUpperKey(upperKey);
    }

    //FIXME(savrus) delay chunkSpec limits until ToProto
    TInputDataSlice::TChunkSliceList chunkSlices;
    for (const auto& slice : dataSlice->ChunkSlices) {
        chunkSlices.push_back(CreateInputChunkSlice(*slice, lowerLimit.Key, upperLimit.Key));
    }

    return New<TInputDataSlice>(
        dataSlice->Type,
        std::move(chunkSlices),
        std::move(lowerLimit),
        std::move(upperLimit));
}

TNullable<TChunkId> IsUnavailable(const TInputDataSlicePtr& dataSlice, bool checkParityParts)
{
    for (const auto& chunkSlice : dataSlice->ChunkSlices) {
        if (IsUnavailable(chunkSlice->GetInputChunk(), checkParityParts)) {
            return chunkSlice->GetInputChunk()->ChunkId();
        }
    }
    return Null;
}

bool CompareDataSlicesByLowerLimit(const TInputDataSlicePtr& slice1, const TInputDataSlicePtr& slice2)
{
    const auto& limit1 = slice1->LowerLimit();
    const auto& limit2 = slice2->LowerLimit();
    i64 diff;

    if (slice1->IsTrivial() && slice2->IsTrivial()) {
        diff = slice1->ChunkSlices[0]->GetInputChunk()->GetRangeIndex() - slice2->ChunkSlices[0]->GetInputChunk()->GetRangeIndex();
        if (diff != 0) {
            return diff < 0;
        }

        diff = (limit1.RowIndex.Get(0) + slice1->ChunkSlices[0]->GetInputChunk()->GetTableRowIndex()) -
            (limit2.RowIndex.Get(0) + slice2->ChunkSlices[0]->GetInputChunk()->GetTableRowIndex());
        if (diff != 0) {
            return diff < 0;
        }
    }

    diff = CompareRows(limit1.Key, limit2.Key);
    return diff < 0;


}

bool CanMergeSlices(const TInputDataSlicePtr& slice1, const TInputDataSlicePtr& slice2)
{
    //FIXME(savrus) really&
    if (!slice1->IsTrivial() || !slice2->IsTrivial()) {
        return false;
    }

    if (slice1->ChunkSlices[0]->GetInputChunk()->GetRangeIndex() != slice2->ChunkSlices[0]->GetInputChunk()->GetRangeIndex()) {
        return false;
    }

    const auto& limit1 = slice1->UpperLimit();
    const auto& limit2 = slice2->LowerLimit();

    if ((limit1.RowIndex || limit1.Key) &&
        limit1.RowIndex.operator bool() == limit2.RowIndex.operator bool() &&
        limit1.Key.operator bool() == limit2.Key.operator bool())
    {
        if (limit1.RowIndex &&
            *limit1.RowIndex + slice1->ChunkSlices[0]->GetInputChunk()->GetTableRowIndex() !=
            *limit2.RowIndex + slice2->ChunkSlices[0]->GetInputChunk()->GetTableRowIndex())
        {
            return false;
        }
        if (limit1.Key && limit1.Key < limit2.Key) {
            return false;
        }
        return true;
    }
    return false;
}

////////////////////////////////////////////////////////////////////////////////

std::vector<TInputDataSlicePtr> CombineVersionedChunkSlices(const std::vector<TInputChunkSlicePtr>& chunkSlices)
{
    std::vector<TInputDataSlicePtr> dataSlices;

    std::vector<std::tuple<TKey, bool, int>> boundaries;
    boundaries.reserve(chunkSlices.size() * 2);
    for (int index = 0; index < chunkSlices.size(); ++index) {
        boundaries.emplace_back(chunkSlices[index]->LowerLimit().Key, false, index);
        boundaries.emplace_back(chunkSlices[index]->UpperLimit().Key, true, index);
    }
    std::sort(boundaries.begin(), boundaries.end());
    yhash_set<int> currentChunks;

    int index = 0;
    while (index < boundaries.size()) {
        const auto& boundary = boundaries[index];
        auto currentKey = std::get<0>(boundary);

        while (index < boundaries.size()) {
            const auto& boundary = boundaries[index];
            auto key = std::get<0>(boundary);
            int chunkIndex = std::get<2>(boundary);
            bool isUpper = std::get<1>(boundary);

            if (key != currentKey) {
                break;
            }

            if (isUpper) {
                currentChunks.erase(chunkIndex);
            } else {
                currentChunks.insert(chunkIndex);
            }
            ++index;
        }

        if (!currentChunks.empty()) {
            std::vector<TInputChunkSlicePtr> chunks;
            for (int chunkIndex : currentChunks) {
                chunks.push_back(chunkSlices[chunkIndex]);
            }

            auto upper = index == boundaries.size() ? MaxKey().Get() : std::get<0>(boundaries[index]);

            auto slice = CreateInputDataSlice(
                EDataSliceDescriptorType::VersionedTable,
                std::move(chunks),
                currentKey,
                upper);

            dataSlices.push_back(std::move(slice));
        }
    }

    return dataSlices;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NChunkClient
} // namespace NYT

