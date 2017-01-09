#pragma once

#include "public.h"

#include <yt/core/misc/nullable.h>
#include <yt/core/misc/property.h>

namespace NYT {
namespace NYson {

////////////////////////////////////////////////////////////////////////////////

//! Contains a sequence of bytes in YSON encoding annotated with EYsonType describing
//! the content. Could be null.
class TYsonString
{
public:
    //! Constructs a null instance.
    TYsonString() = default;

    //! Constructs an non-null instance with given type and content.
    explicit TYsonString(
        const Stroka& data,
        EYsonType type = EYsonType::Node);

    //! Returns |true| if the instance is not null.
    explicit operator bool() const;

    const Stroka& GetData() const;
    EYsonType GetType() const;

    //! If the instance is not null, invokes the parser (which may throw).
    void Validate() const;

    void Save(TStreamSaveContext& context) const;
    void Load(TStreamLoadContext& context);

private:
    Stroka Data_;
    EYsonType Type_ = EYsonType::None;

};

////////////////////////////////////////////////////////////////////////////////

void Serialize(const TYsonString& yson, IYsonConsumer* consumer);

bool operator == (const TYsonString& lhs, const TYsonString& rhs);
bool operator != (const TYsonString& lhs, const TYsonString& rhs);

Stroka ToString(const TYsonString& yson);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYson
} // namespace NYT

//! A hasher for TYsonString
template <>
struct hash<NYT::NYson::TYsonString>
{
    size_t operator () (const NYT::NYson::TYsonString& str) const
    {
        return THash<Stroka>()(str.GetData());
    }
};

////////////////////////////////////////////////////////////////////////////////
