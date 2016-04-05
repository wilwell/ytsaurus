#include "cg_routines.h"
#include "callbacks.h"
#include "cg_types.h"
#include "evaluation_helpers.h"
#include "helpers.h"
#include "query_statistics.h"

#include <yt/ytlib/chunk_client/chunk_spec.h>

#include <yt/ytlib/table_client/row_buffer.h>
#include <yt/ytlib/table_client/schemaful_reader.h>
#include <yt/ytlib/table_client/schemaful_writer.h>
#include <yt/ytlib/table_client/unordered_schemaful_reader.h>
#include <yt/ytlib/table_client/unversioned_row.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/misc/farm_hash.h>

#include <yt/core/profiling/scoped_timer.h>

#include <contrib/libs/re2/re2/re2.h>

#include <mutex>

#include <string.h>

namespace llvm {

template <bool Cross>
class TypeBuilder<google::re2::RE2*, Cross>
    : public TypeBuilder<void*, Cross>
{ };

} // namespace llvm

////////////////////////////////////////////////////////////////////////////////

namespace NYT {
namespace NQueryClient {
namespace NRoutines {

using namespace NConcurrency;
using namespace NTableClient;

static const auto& Logger = QueryClientLogger;

////////////////////////////////////////////////////////////////////////////////

#ifndef NDEBUG
#define CHECK_STACK() \
    { \
        int dummy; \
        size_t currentStackSize = context->StackSizeGuardHelper - reinterpret_cast<intptr_t>(&dummy); \
        YCHECK(currentStackSize < 10000); \
    }
#else
#define CHECK_STACK() (void) 0;
#endif

////////////////////////////////////////////////////////////////////////////////

TRow* GetRowsData(std::vector<TRow>* rows)
{
    return rows->data();
}

int GetRowsSize(std::vector<TRow>* rows)
{
    return rows->size();
}

////////////////////////////////////////////////////////////////////////////////

void WriteRow(TRow row, TExecutionContext* context)
{
    CHECK_STACK();

    if (context->RowsWritten >= context->Limit) {
        throw TInterruptedCompleteException();
    }

    if (context->RowsWritten >= context->OutputRowLimit) {
        throw TInterruptedIncompleteException();
    }

    ++context->RowsWritten;

    auto* batch = context->OutputRowsBatch;

    const auto& rowBuffer = context->OutputBuffer;

    YASSERT(batch->size() < batch->capacity());
    batch->push_back(rowBuffer->Capture(row));

    if (batch->size() == batch->capacity()) {
        auto& writer = context->Writer;
        bool shouldNotWait;
        {
            NProfiling::TAggregatingTimingGuard timingGuard(&context->Statistics->WriteTime);
            shouldNotWait = writer->Write(*batch);
        }

        if (!shouldNotWait) {
            NProfiling::TAggregatingTimingGuard timingGuard(&context->Statistics->AsyncTime);
            WaitFor(writer->GetReadyEvent())
                .ThrowOnError();
        }
        batch->clear();
        rowBuffer->Clear();
    }
}

void ScanOpHelper(
    TExecutionContext* context,
    void** consumeRowsClosure,
    void (*consumeRows)(void** closure, TRow* rows, int size))
{
    auto& reader = context->Reader;

    std::vector<TRow> rows;
    rows.reserve(MaxRowsPerRead);

    while (true) {
        context->IntermediateBuffer->Clear();

        bool hasMoreData;
        {
            NProfiling::TAggregatingTimingGuard timingGuard(&context->Statistics->ReadTime);
            hasMoreData = reader->Read(&rows);
        }

        bool shouldWait = rows.empty();

        // Remove null rows.
        rows.erase(
            std::remove_if(rows.begin(), rows.end(), [] (TRow row) {
                return !row;
            }),
            rows.end());

        if (context->RowsRead + rows.size() >= context->InputRowLimit) {
            YCHECK(context->RowsRead <= context->InputRowLimit);
            rows.resize(context->InputRowLimit - context->RowsRead);
            context->Statistics->IncompleteInput = true;
            hasMoreData = false;
        }
        context->RowsRead += rows.size();

        consumeRows(consumeRowsClosure, rows.data(), rows.size());
        rows.clear();

        if (!hasMoreData) {
            break;
        }

        if (shouldWait) {
            NProfiling::TAggregatingTimingGuard timingGuard(&context->Statistics->AsyncTime);
            WaitFor(reader->GetReadyEvent())
                .ThrowOnError();
        }
    }
}

void InsertJoinRow(
    TExecutionContext* context,
    TJoinLookup* lookup,
    std::vector<TRow>* keys,
    std::vector<std::pair<TRow, i64>>* chainedRows,
    TMutableRow* keyPtr,
    TRow row,
    int keySize)
{
    CHECK_STACK();

    i64 chainIndex = chainedRows->size();
    chainedRows->emplace_back(context->PermanentBuffer->Capture(row), -1);

    if (chainIndex >= context->JoinRowLimit) {
        throw TInterruptedIncompleteException();
    }

    TMutableRow key = *keyPtr;
    auto inserted = lookup->insert(std::make_pair(key, std::make_pair(chainIndex, false)));
    if (inserted.second) {
        keys->push_back(key);
        for (int index = 0; index < keySize; ++index) {
            context->PermanentBuffer->Capture(&key[index]);
        }
        *keyPtr = TMutableRow::Allocate(context->PermanentBuffer->GetPool(), keySize);
    } else {
        auto& startIndex = inserted.first->second.first;
        chainedRows->back().second = startIndex;
        startIndex = chainIndex;
    }
}

void SaveJoinRow(
    TExecutionContext* context,
    std::vector<TRow>* rows,
    TRow row)
{
    CHECK_STACK();

    rows->push_back(context->PermanentBuffer->Capture(row));
}

void JoinOpHelper(
    TExecutionContext* context,
    int index,
    THasherFunction* lookupHasher,
    TComparerFunction* lookupEqComparer,
    TComparerFunction* lookupLessComparer,
    void** collectRowsClosure,
    void (*collectRows)(
        void** closure,
        TJoinLookup* joinLookup,
        std::vector<TRow>* keys,
        std::vector<std::pair<TRow, i64>>* chainedRows),
    void** consumeRowsClosure,
    void (*consumeRows)(void** closure, std::vector<TRow>* rows))
{
    TJoinLookup joinLookup(
        InitialGroupOpHashtableCapacity,
        lookupHasher,
        lookupEqComparer);

    std::vector<TRow> keys;
    std::vector<std::pair<TRow, i64>> chainedRows;

    joinLookup.set_empty_key(TRow());

    try {
        // Collect join ids.
        collectRows(collectRowsClosure, &joinLookup, &keys, &chainedRows);
    } catch (const TInterruptedIncompleteException&) {
        // Set incomplete and continue
        context->Statistics->IncompleteOutput = true;
    }

    LOG_DEBUG("Sorting %v join keys",
        keys.size());

    std::sort(keys.begin(), keys.end(), lookupLessComparer);

    LOG_DEBUG("Collected %v join keys from %v rows",
        keys.size(),
        chainedRows.size());

    std::vector<TRow> joinedRows;
    try {
        context->JoinEvaluators[index](
            context,
            lookupHasher,
            lookupEqComparer,
            joinLookup,
            std::move(keys),
            std::move(chainedRows),
            context->PermanentBuffer,
            &joinedRows);
    } catch (const TInterruptedIncompleteException&) {
        // Set incomplete and continue
        context->Statistics->IncompleteOutput = true;
    }

    LOG_DEBUG("Joined into %v rows",
        joinedRows.size());

    // Consume joined rows.
    consumeRows(consumeRowsClosure, &joinedRows);
}

void GroupOpHelper(
    TExecutionContext* context,
    THasherFunction* groupHasher,
    TComparerFunction* groupComparer,
    void** collectRowsClosure,
    void (*collectRows)(void** closure, std::vector<TRow>* groupedRows, TLookupRows* lookupRows),
    void** consumeRowsClosure,
    void (*consumeRows)(void** closure, std::vector<TRow>* groupedRows))
{
    std::vector<TRow> groupedRows;
    TLookupRows lookupRows(
        InitialGroupOpHashtableCapacity,
        groupHasher,
        groupComparer);

    lookupRows.set_empty_key(TRow());

    try {
        collectRows(collectRowsClosure, &groupedRows, &lookupRows);
    } catch (const TInterruptedIncompleteException&) {
        // Set incomplete and continue
        context->Statistics->IncompleteOutput = true;
    }

    LOG_DEBUG("Collected %v group rows",
        groupedRows.size());

    consumeRows(consumeRowsClosure, &groupedRows);
}

void AllocatePermanentRow(TExecutionContext* context, int valueCount, TMutableRow* row)
{
    CHECK_STACK();

    *row = TMutableRow::Allocate(context->PermanentBuffer->GetPool(), valueCount);
}

const TRow* InsertGroupRow(
    TExecutionContext* context,
    TLookupRows* lookupRows,
    std::vector<TRow>* groupedRows,
    TMutableRow row,
    int keySize,
    bool checkNulls)
{
    CHECK_STACK();

    auto inserted = lookupRows->insert(row);

    if (inserted.second) {
        if (groupedRows->size() >= context->GroupRowLimit) {
            throw TInterruptedIncompleteException();
        }

        groupedRows->push_back(row);
        for (int index = 0; index < keySize; ++index) {
            context->PermanentBuffer->Capture(&row[index]);
        }

        if (checkNulls) {
            for (int index = 0; index < keySize; ++index) {
                if (row[index].Type == EValueType::Null) {
                    THROW_ERROR_EXCEPTION("Null values in group key");
                }
            }
        }
    }

    return &*inserted.first;
}

void AllocateIntermediateRow(TExpressionContext* context, int valueCount, TMutableRow* row)
{
    CHECK_STACK();

    *row = TMutableRow::Allocate(context->IntermediateBuffer->GetPool(), valueCount);
}

void AddRow(TTopCollector* topCollector, TRow row)
{
    topCollector->AddRow(row);
}

void OrderOpHelper(
    TExecutionContext* context,
    TComparerFunction* comparer,
    void** collectRowsClosure,
    void (*collectRows)(void** closure, TTopCollector* topCollector),
    void** consumeRowsClosure,
    void (*consumeRows)(void** closure, std::vector<TMutableRow>* rows),
    int rowSize)
{
    auto limit = context->Limit;

    TTopCollector topCollector(limit, comparer);
    collectRows(collectRowsClosure, &topCollector);
    auto rows = topCollector.GetRows(rowSize);

    // Consume joined rows.
    consumeRows(consumeRowsClosure, &rows);
}

char* AllocateBytes(TExecutionContext* context, size_t byteCount)
{
    CHECK_STACK();

    return context
        ->IntermediateBuffer
        ->GetPool()
        ->AllocateUnaligned(byteCount);
}

char* AllocatePermanentBytes(TExecutionContext* context, size_t byteCount)
{
    CHECK_STACK();

    return context
        ->PermanentBuffer
        ->GetPool()
        ->AllocateUnaligned(byteCount);
}

////////////////////////////////////////////////////////////////////////////////

char IsRowInArray(
    TExpressionContext* context,
    TComparerFunction* comparer,
    TRow row,
    int index)
{
    // TODO(lukyan): check null
    const auto& rows = (*context->LiteralRows)[index];
    return std::binary_search(rows.Begin(), rows.End(), row, comparer);
}

size_t StringHash(
    const char* data,
    ui32 length)
{
    return FarmHash(data, length);
}

// FarmHash and MurmurHash hybrid to hash TRow.
ui64 SimpleHash(const TUnversionedValue* begin, const TUnversionedValue* end)
{
    const ui64 MurmurHashConstant = 0xc6a4a7935bd1e995ULL;

    // Append fingerprint to hash value. Like Murmurhash.
    const auto hash64 = [&, MurmurHashConstant] (ui64 data, ui64 value) {
        value ^= FarmFingerprint(data);
        value *= MurmurHashConstant;
        return value;
    };

    // Hash string. Like Murmurhash.
    const auto hash = [&, MurmurHashConstant] (const void* voidData, int length, ui64 seed) {
        ui64 result = seed;
        const ui64* ui64Data = reinterpret_cast<const ui64*>(voidData);
        const ui64* ui64End = ui64Data + (length / 8);

        while (ui64Data < ui64End) {
            auto data = *ui64Data++;
            result = hash64(data, result);
        }

        const char* charData = reinterpret_cast<const char*>(ui64Data);

        if (length & 4) {
            result ^= (*reinterpret_cast<const ui32*>(charData) << (length & 3));
            charData += 4;
        }
        if (length & 2) {
            result ^= (*reinterpret_cast<const ui16*>(charData) << (length & 1));
            charData += 2;
        }
        if (length & 1) {
            result ^= *reinterpret_cast<const ui8*>(charData);
        }

        result *= MurmurHashConstant;
        result ^= (result >> 47);
        result *= MurmurHashConstant;
        result ^= (result >> 47);
        return result;
    };

    ui64 result = end - begin;

    for (auto value = begin; value != end; value++) {
        switch(value->Type) {
            case EValueType::Int64:
                result = hash64(value->Data.Int64, result);
                break;
            case EValueType::Uint64:
                result = hash64(value->Data.Uint64, result);
                break;
            case EValueType::Boolean:
                result = hash64(value->Data.Boolean, result);
                break;
            case EValueType::String:
                result = hash(
                    value->Data.String,
                    value->Length,
                    result);
                break;
            case EValueType::Null:
                result = hash64(0, result);
                break;
            default:
                YUNREACHABLE();
        }
    }

    return result;
}

ui64 FarmHashUint64(ui64 value)
{
    return FarmFingerprint(value);
}

void ThrowException(const char* error)
{
    THROW_ERROR_EXCEPTION("Error while executing UDF: %s", error);
}

google::re2::RE2* RegexCreate(TUnversionedValue* regexp)
{
    return new google::re2::RE2(google::re2::StringPiece(regexp->Data.String, regexp->Length));
}

void RegexDestroy(google::re2::RE2* re2)
{
    delete re2;
}

ui8 RegexFullMatch(google::re2::RE2* re2, TUnversionedValue* string)
{
    YCHECK(string->Type == EValueType::String);

    return google::re2::RE2::FullMatch(
        google::re2::StringPiece(string->Data.String, string->Length),
        *re2);
}

ui8 RegexPartialMatch(google::re2::RE2* re2, TUnversionedValue* string)
{
    YCHECK(string->Type == EValueType::String);

    return google::re2::RE2::PartialMatch(
        google::re2::StringPiece(string->Data.String, string->Length),
        *re2);
}

void CopyString(TExecutionContext* context, TUnversionedValue* result, const std::string& str)
{
    char* data = AllocatePermanentBytes(context, str.size());
    memcpy(data, str.c_str(), str.size());
    result->Type = EValueType::String;
    result->Length = str.size();
    result->Data.String = data;
}

void RegexReplaceFirst(
    TExecutionContext* context,
    google::re2::RE2* re2,
    TUnversionedValue* string,
    TUnversionedValue* rewrite,
    TUnversionedValue* result)
{
    YCHECK(string->Type == EValueType::String);
    YCHECK(rewrite->Type == EValueType::String);

    google::re2::string str(string->Data.String, string->Length);
    google::re2::RE2::Replace(
        &str,
        *re2,
        google::re2::StringPiece(rewrite->Data.String, rewrite->Length));

    CopyString(context, result, str);
}


void RegexReplaceAll(
    TExecutionContext* context,
    google::re2::RE2* re2,
    TUnversionedValue* string,
    TUnversionedValue* rewrite,
    TUnversionedValue* result)
{
    YCHECK(string->Type == EValueType::String);
    YCHECK(rewrite->Type == EValueType::String);

    google::re2::string str(string->Data.String, string->Length);
    google::re2::RE2::GlobalReplace(
        &str,
        *re2,
        google::re2::StringPiece(rewrite->Data.String, rewrite->Length));

    CopyString(context, result, str);
}

void RegexExtract(
    TExecutionContext* context,
    google::re2::RE2* re2,
    TUnversionedValue* string,
    TUnversionedValue* rewrite,
    TUnversionedValue* result)
{
    YCHECK(string->Type == EValueType::String);
    YCHECK(rewrite->Type == EValueType::String);

    google::re2::string str;
    google::re2::RE2::Extract(
        google::re2::StringPiece(string->Data.String, string->Length),
        *re2,
        google::re2::StringPiece(rewrite->Data.String, rewrite->Length),
        &str);

    CopyString(context, result, str);
}

void RegexEscape(
    TExecutionContext* context,
    TUnversionedValue* string,
    TUnversionedValue* result)
{
    auto str = google::re2::RE2::QuoteMeta(
        google::re2::StringPiece(string->Data.String, string->Length));

    CopyString(context, result, str);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NRoutines

////////////////////////////////////////////////////////////////////////////////

using NCodegen::TRoutineRegistry;

void RegisterQueryRoutinesImpl(TRoutineRegistry* registry)
{
#define REGISTER_ROUTINE(routine) \
    registry->RegisterRoutine(#routine, NRoutines::routine)
    REGISTER_ROUTINE(WriteRow);
    REGISTER_ROUTINE(ScanOpHelper);
    REGISTER_ROUTINE(JoinOpHelper);
    REGISTER_ROUTINE(GroupOpHelper);
    REGISTER_ROUTINE(StringHash);
    REGISTER_ROUTINE(InsertGroupRow);
    REGISTER_ROUTINE(InsertJoinRow);
    REGISTER_ROUTINE(SaveJoinRow);
    REGISTER_ROUTINE(AllocatePermanentRow);
    REGISTER_ROUTINE(AllocateIntermediateRow);
    REGISTER_ROUTINE(AllocatePermanentBytes);
    REGISTER_ROUTINE(AllocateBytes);
    REGISTER_ROUTINE(GetRowsData);
    REGISTER_ROUTINE(GetRowsSize);
    REGISTER_ROUTINE(IsRowInArray);
    REGISTER_ROUTINE(SimpleHash);
    REGISTER_ROUTINE(FarmHashUint64);
    REGISTER_ROUTINE(AddRow);
    REGISTER_ROUTINE(OrderOpHelper);
    REGISTER_ROUTINE(ThrowException);
    REGISTER_ROUTINE(RegexCreate);
    REGISTER_ROUTINE(RegexDestroy);
    REGISTER_ROUTINE(RegexFullMatch);
    REGISTER_ROUTINE(RegexPartialMatch);
    REGISTER_ROUTINE(RegexReplaceFirst);
    REGISTER_ROUTINE(RegexReplaceAll);
    REGISTER_ROUTINE(RegexExtract);
    REGISTER_ROUTINE(RegexEscape);
#undef REGISTER_ROUTINE

    registry->RegisterRoutine("memcmp", std::memcmp);
}

TRoutineRegistry* GetQueryRoutineRegistry()
{
    static TRoutineRegistry registry;
    static std::once_flag onceFlag;
    std::call_once(onceFlag, &RegisterQueryRoutinesImpl, &registry);
    return &registry;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

