#include "public.h"

#include <ytlib/chunk_client/schema.h>
#include <ytlib/chunk_client/read_limit.h>

#include <ytlib/new_table_client/unversioned_row.h>
#include <ytlib/new_table_client/schema.h>

#include <core/misc/protobuf_helpers.h>

#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/table_client/chunk_meta_extensions.h>
#include <ytlib/new_table_client/chunk_meta_extensions.h>

namespace NYT {
namespace NQueryClient {

using NChunkClient::TReadLimit;
using NVersionedTableClient::MinKey;
using NVersionedTableClient::MaxKey;

typedef NChunkClient::NProto::TMiscExt TMiscProto;
typedef NVersionedTableClient::NProto::TTableSchemaExt TTableSchemaProto;
typedef NTableClient::NProto::TBoundaryKeysExt TBoundaryKeysProto;
typedef NTableClient::NProto::TKeyColumnsExt TKeyColumnsProto;

////////////////////////////////////////////////////////////////////////////////

NObjectClient::TObjectId GetObjectIdFromDataSplit(const TDataSplit& dataSplit)
{
    return NYT::FromProto<NObjectClient::TObjectId>(dataSplit.chunk_id());
}

TTableSchema GetTableSchemaFromDataSplit(const TDataSplit& dataSplit)
{
    auto tableSchemaProto = GetProtoExtension<TTableSchemaProto>(
        dataSplit.chunk_meta().extensions());
    return NYT::FromProto<TTableSchema>(tableSchemaProto);
}

TKeyColumns GetKeyColumnsFromDataSplit(const TDataSplit& dataSplit)
{
    auto keyColumnsProto = GetProtoExtension<TKeyColumnsProto>(
        dataSplit.chunk_meta().extensions());
    return NYT::FromProto<Stroka>(keyColumnsProto.names());
}

TKey GetLowerBoundFromDataSplit(const TDataSplit& dataSplit)
{
    if (dataSplit.has_start_limit()) {
        auto readLimit = FromProto<TReadLimit>(dataSplit.start_limit());
        return readLimit.GetKey();
    } else {
        return MinKey();
    }
}

TKey GetUpperBoundFromDataSplit(const TDataSplit& dataSplit)
{
    if (dataSplit.has_end_limit()) {
        auto readLimit = FromProto<TReadLimit>(dataSplit.end_limit());
        return readLimit.GetKey();
    } else {
        return MaxKey();
    }
}

TKeyRange GetBothBoundsFromDataSplit(const TDataSplit& dataSplit)
{
    return std::make_pair(
        GetLowerBoundFromDataSplit(dataSplit),
        GetUpperBoundFromDataSplit(dataSplit));
}

bool IsSorted(const TDataSplit& dataSplit)
{
    auto miscProto = GetProtoExtension<TMiscProto>(
        dataSplit.chunk_meta().extensions());
    return miscProto.sorted();
}

void SetObjectId(TDataSplit* dataSplit, const NObjectClient::TObjectId& objectId)
{
    ToProto(dataSplit->mutable_chunk_id(), objectId);
}

void SetTableSchema(TDataSplit* dataSplit, const TTableSchema& tableSchema)
{
    TTableSchemaProto tableSchemaProto;
    ToProto(&tableSchemaProto, tableSchema);
    SetProtoExtension<TTableSchemaProto>(
        dataSplit->mutable_chunk_meta()->mutable_extensions(),
        tableSchemaProto);
}

void SetKeyColumns(TDataSplit* dataSplit, const TKeyColumns& keyColumns)
{
    TKeyColumnsProto keyColumnsProto;
    ToProto(keyColumnsProto.mutable_names(), keyColumns);
    SetProtoExtension<TKeyColumnsProto>(
        dataSplit->mutable_chunk_meta()->mutable_extensions(),
        keyColumnsProto);
}

void SetLowerBound(TDataSplit* dataSplit, const TKey& lowerBound)
{
    if (lowerBound == MinKey()) {
        dataSplit->clear_start_limit();
        return;
    }
    TReadLimit readLimit;
    readLimit.SetKey(lowerBound);
    ToProto(dataSplit->mutable_start_limit(), readLimit);
}

void SetUpperBound(TDataSplit* dataSplit, const TKey& upperBound)
{
    if (upperBound == MaxKey()) {
        dataSplit->clear_end_limit();
        return;
    }
    TReadLimit readLimit;
    readLimit.SetKey(upperBound);
    ToProto(dataSplit->mutable_end_limit(), readLimit);
}

void SetBothBounds(TDataSplit* dataSplit, const TKeyRange& keyRange)
{
    SetLowerBound(dataSplit, keyRange.first);
    SetUpperBound(dataSplit, keyRange.second);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT

