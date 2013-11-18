#include "stdafx.h"
#include "chunk_writer.h"
#include "row.h"
#include "name_table.h"
#include "block_writer.h"
#include "chunk_meta_extensions.h"
#include "private.h"

#include <ytlib/table_client/chunk_meta_extensions.h>

#include <ytlib/chunk_client/async_writer.h>
#include <ytlib/chunk_client/encoding_writer.h>
#include <ytlib/chunk_client/chunk_meta_extensions.h>
#include <ytlib/chunk_client/dispatcher.h>

#include <core/concurrency/fiber.h>


namespace NYT {
namespace NVersionedTableClient {

using namespace NChunkClient;
using namespace NConcurrency;

static const int TimestampIndex = 0;

////////////////////////////////////////////////////////////////////////////////

TChunkWriter::TChunkWriter(
    TChunkWriterConfigPtr config,
    TEncodingWriterOptionsPtr options,
    IAsyncWriterPtr asyncWriter)
    : Config(config)
    , Options(options)
    , UnderlyingWriter(asyncWriter)
    , OutputNameTable(New<TNameTable>())
    , EncodingWriter(New<TEncodingWriter>(config, options, asyncWriter))
    , IsNewKey(false)
    , RowIndex(0)
    , LargestBlockSize(0)
{ }

void TChunkWriter::Open(
    TNameTablePtr nameTable,
    const TTableSchema& schema,
    const TKeyColumns& keyColumns,
    ERowsetType rowsetType)
{
    Schema = schema;
    RowsetType = rowsetType;
    InputNameTable = nameTable;

    if (RowsetType == ERowsetType::Versioned) {
        // Block writer treats timestamps as integers.
        ColumnSizes.push_back(ERowValueType::Integer);
    }

    // Integers and Doubles align at 8 bytes (stores the whole value),
    // while String and Any align at 4 bytes (stores just offset to value).
    // To ensure proper alignment during reading, move all integer and
    // double columns to the front.
    std::sort(
        Schema.Columns().begin(),
        Schema.Columns().end(),
        [] (const TColumnSchema& lhs, const TColumnSchema& rhs) {
            auto isFront = [] (const TColumnSchema& schema) {
                return schema.Type == ERowValueType::Integer || schema.Type == ERowValueType::Double;
            };
            if (isFront(lhs) && !isFront(rhs)) {
                return true;
            }
            if (!isFront(lhs) && isFront(rhs)) {
                return false;
            }
            return &lhs < &rhs;
        }
    );

    ColumnDescriptors.resize(InputNameTable->GetSize());

    for (const auto& column : Schema.Columns()) {
        TColumnDescriptor descriptor;
        descriptor.IndexInBlock = ColumnSizes.size();
        descriptor.OutputIndex = OutputNameTable->RegisterName(column.Name);
        descriptor.Type = column.Type;

        if (column.Type == ERowValueType::String || column.Type == ERowValueType::Any) {
            ColumnSizes.push_back(4);
        } else {
            ColumnSizes.push_back(8);
        }

        auto id = InputNameTable->GetId(column.Name);
        ColumnDescriptors[id] = descriptor;
    }

    for (const auto& column : keyColumns) {
        auto id = InputNameTable->GetId(column);
        KeyIds.push_back(id);

        auto& descriptor = ColumnDescriptors[id];
        YCHECK(descriptor.IndexInBlock >= 0);
        YCHECK(descriptor.Type != ERowValueType::Any);
        descriptor.IsKeyPart = true;
    }

    CurrentBlock.reset(new TBlockWriter(ColumnSizes));
}

void TChunkWriter::WriteValue(const TVersionedValue& value)
{
    if (ColumnDescriptors.size() <= value.Id) {
        ColumnDescriptors.resize(value.Id + 1);
    }

    auto& columnDescriptor = ColumnDescriptors[value.Id];

    if (columnDescriptor.Type == ERowValueType::Null) {
        // Uninitialized column becomes variable.
        columnDescriptor.Type = ERowValueType::TheBottom;
        columnDescriptor.OutputIndex =
            OutputNameTable->RegisterName(InputNameTable->GetName(value.Id));
    }

    switch (columnDescriptor.Type) {
        case ERowValueType::Integer:
            YASSERT(value.Type == ERowValueType::Integer || value.Type == ERowValueType::Null);
            CurrentBlock->WriteInteger(value, columnDescriptor.IndexInBlock);
            if (columnDescriptor.IsKeyPart) {
                if (value.Data.Integer != columnDescriptor.PreviousValue.Integer) {
                    IsNewKey = true;
                }
                columnDescriptor.PreviousValue.Integer = value.Data.Integer;
            }
            break;

        case ERowValueType::Double:
            YASSERT(value.Type == ERowValueType::Double || value.Type == ERowValueType::Null);
            CurrentBlock->WriteDouble(value, columnDescriptor.IndexInBlock);
            if (columnDescriptor.IsKeyPart) {
                if (value.Data.Double != columnDescriptor.PreviousValue.Double) {
                    IsNewKey = true;
                }
                columnDescriptor.PreviousValue.Double = value.Data.Double;
            }
            break;

        case ERowValueType::String:
            YASSERT(value.Type == ERowValueType::String || value.Type == ERowValueType::Null);
            if (columnDescriptor.IsKeyPart) {
                auto newKey = CurrentBlock->WriteKeyString(value, columnDescriptor.IndexInBlock);
                auto oldKey = TStringBuf(
                    columnDescriptor.PreviousValue.String,
                    columnDescriptor.PreviousValue.Length);
                if (newKey != oldKey) {
                    IsNewKey = true;
                }
                columnDescriptor.PreviousValue.String = newKey.data();
                columnDescriptor.PreviousValue.Length = newKey.length();
            } else {
                CurrentBlock->WriteString(value, columnDescriptor.IndexInBlock);
            }
            break;

        case ERowValueType::Any:
            CurrentBlock->WriteAny(value, columnDescriptor.IndexInBlock);
            break;

        // Variable column.
        case ERowValueType::TheBottom:
            CurrentBlock->WriteVariable(value, columnDescriptor.OutputIndex);
            break;

        default:
            YUNREACHABLE();
    }
}

void TChunkWriter::WriteValue(const TUnversionedValue& value)
{
    // TODO(babenko): avoid copying
    TVersionedValue versionedValue;
    static_cast<TUnversionedValue&>(versionedValue) = value;
    versionedValue.Timestamp = NullTimestamp;
    WriteValue(versionedValue);
}

bool TChunkWriter::EndRow()
{
    if (RowsetType == ERowsetType::Versioned && RowIndex > 0) {
        if (PreviousBlock) {
            PreviousBlock->PushEndOfKey(IsNewKey);
        } else {
            CurrentBlock->PushEndOfKey(IsNewKey);
        }
        // TODO(babenko): fixme
        //CurrentBlock->WriteTimestamp(timestamp, deleted, TimestampIndex);
        IsNewKey = false;
    } else {
        // TODO(babenko): fixme
        //YASSERT(timestamp == NullTimestamp);
    }

    CurrentBlock->EndRow();

    if (PreviousBlock) {
        FlushPreviousBlock();

        if (!KeyIds.empty()) {
            auto* key = IndexExt.add_keys();
            for (const auto& id : KeyIds) {
                auto* part = key->add_parts();
                const auto& column = ColumnDescriptors[id];
                part->set_type(column.Type);
                switch (column.Type) {
                    case ERowValueType::Integer:
                        part->set_int_value(column.PreviousValue.Integer);
                        break;
                    case ERowValueType::Double:
                        part->set_double_value(column.PreviousValue.Double);
                        break;
                    case ERowValueType::String:
                        part->set_str_value(
                            column.PreviousValue.String,
                            column.PreviousValue.Length);
                        break;
                    default:
                        YUNREACHABLE();
                }
            }
        }
    }

    if (CurrentBlock->GetSize() > Config->BlockSize) {
        YCHECK(PreviousBlock.get() == nullptr);
        PreviousBlock.swap(CurrentBlock);
        CurrentBlock.reset(new TBlockWriter(ColumnSizes));
    }

    ++RowIndex;
    return EncodingWriter->IsReady();
}

TAsyncError TChunkWriter::GetReadyEvent()
{
    return EncodingWriter->GetReadyEvent();
}

TAsyncError TChunkWriter::AsyncClose()
{
    auto result = NewPromise<TError>();

    TDispatcher::Get()->GetWriterInvoker()->Invoke(BIND(
        &TChunkWriter::DoClose,
        MakeWeak(this),
        result));

    return result;
}

i64 TChunkWriter::GetRowIndex() const 
{
    return RowIndex;
}

void TChunkWriter::DoClose(TAsyncErrorPromise result)
{
    if (CurrentBlock->GetSize() > 0) {
        YCHECK(PreviousBlock == nullptr);
        PreviousBlock.swap(CurrentBlock);
    }

    if (PreviousBlock) {
        FlushPreviousBlock();
    }

    {
        auto error = WaitFor(EncodingWriter->AsyncFlush());
        if (!error.IsOK()) {
            result.Set(error);
            return;
        }
    }

    Meta.set_type(EChunkType::Table);
    Meta.set_version(FormatVersion);

    SetProtoExtension(Meta.mutable_extensions(), BlockMetaExt);
    SetProtoExtension(Meta.mutable_extensions(), NYT::ToProto<NProto::TTableSchemaExt>(Schema));

    NProto::TNameTableExt nameTableExt;
    ToProto(&nameTableExt, OutputNameTable);
    SetProtoExtension(Meta.mutable_extensions(), nameTableExt);

    NChunkClient::NProto::TMiscExt miscExt;
    if (KeyIds.empty()) {
        miscExt.set_sorted(false);
    } else {
        miscExt.set_sorted(true);

        SetProtoExtension(Meta.mutable_extensions(), IndexExt);
        NTableClient::NProto::TKeyColumnsExt keyColumnsExt;
        for (int id : KeyIds) {
            keyColumnsExt.add_names(InputNameTable->GetName(id));
        }
        SetProtoExtension(Meta.mutable_extensions(), keyColumnsExt);
    }

    miscExt.set_uncompressed_data_size(EncodingWriter->GetUncompressedSize());
    miscExt.set_compressed_data_size(EncodingWriter->GetCompressedSize());
    miscExt.set_meta_size(Meta.ByteSize());
    miscExt.set_compression_codec(Options->CompressionCodec);
    miscExt.set_row_count(RowIndex);
    miscExt.set_max_block_size(LargestBlockSize);
    SetProtoExtension(Meta.mutable_extensions(), miscExt);

    auto error = WaitFor(UnderlyingWriter->AsyncClose(Meta));
    result.Set(error);
}

void TChunkWriter::FlushPreviousBlock()
{
    auto block = PreviousBlock->FlushBlock();
    EncodingWriter->WriteBlock(std::move(block.Data));
    *BlockMetaExt.add_items() = block.Meta;
    if (block.Meta.block_size() > LargestBlockSize) {
        LargestBlockSize = block.Meta.block_size();
    }
    PreviousBlock.reset();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
