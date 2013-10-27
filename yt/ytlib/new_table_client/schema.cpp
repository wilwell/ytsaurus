#include "stdafx.h"
#include "schema.h"

#include <core/ytree/serialize.h>

#include <core/ytree/convert.h>

#include <core/misc/protobuf_helpers.h>

#include <ytlib/new_table_client/chunk_meta.pb.h>

namespace NYT {
namespace NVersionedTableClient {

using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

TColumnSchema::TColumnSchema()
    : Type(EColumnType::Null)
{ }

TColumnSchema::TColumnSchema( const Stroka& name, EColumnType type )
    : Name(name)
    , Type(type)
{ }

struct TSerializableColumnSchema
    : public TYsonSerializableLite
    , public TColumnSchema
{
    TSerializableColumnSchema()
    { }

    explicit TSerializableColumnSchema(const TColumnSchema& other)
        : TColumnSchema(other)
    {
        RegisterParameter("name", Name);
        RegisterParameter("type", Type);
    }
};

void Serialize(const TColumnSchema& schema, IYsonConsumer* consumer)
{
    TSerializableColumnSchema wrapper(schema);
    Serialize(static_cast<const TYsonSerializableLite&>(wrapper), consumer);
}

void Deserialize(TColumnSchema& schema, INodePtr node)
{
    TSerializableColumnSchema wrapper;
    Deserialize(static_cast<TYsonSerializableLite&>(wrapper), node);
    // TODO(babenko): we shouldn't be concerned with manual validation here
    wrapper.Validate();
    schema = static_cast<TColumnSchema&>(wrapper);
}

void ToProto(NProto::TColumnSchema* protoSchema, const TColumnSchema& schema)
{
    protoSchema->set_name(schema.Name);
    protoSchema->set_type(schema.Type);
}

void FromProto(TColumnSchema* schema, const NProto::TColumnSchema& protoSchema)
{
    schema->Name = protoSchema.name();
    schema->Type = EColumnType(protoSchema.type());
}

////////////////////////////////////////////////////////////////////////////////

TColumnSchema* TTableSchema::FindColumn(const TStringBuf& name)
{
    for (auto& column : Columns_) {
        if (column.Name == name) {
            return &column;
        }
    }
    return nullptr;
}

TColumnSchema& TTableSchema::GetColumnOrThrow(const TStringBuf& name)
{
    auto* column = FindColumn(name);
    if (!column) {
        THROW_ERROR_EXCEPTION("Missing schema column %s",
            ~Stroka(name).Quote());
    }
    return *column;
}

int TTableSchema::GetColumnIndex(const TColumnSchema& column)
{
    return &column - Columns().data();
}

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TTableSchema& schema, IYsonConsumer* consumer)
{
    NYTree::Serialize(schema.Columns(), consumer);
}

void Deserialize(TTableSchema& schema, INodePtr node)
{
    NYTree::Deserialize(schema.Columns(), node);
}

void ToProto(NProto::TTableSchemaExt* protoSchema, const TTableSchema& schema)
{
    NYT::ToProto(protoSchema->mutable_columns(), schema.Columns());
}

void FromProto(TTableSchema* schema, const NProto::TTableSchemaExt& protoSchema)
{
    schema->Columns() = NYT::FromProto<TColumnSchema>(protoSchema.columns());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NVersionedTableClient
} // namespace NYT
