#include "null_yson_consumer.h"
#include "yson_string.h"

#include <yt/core/misc/common.h>

#include <yt/core/yson/consumer.h>

namespace NYT {
namespace NYTree {

using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

class TNullYsonConsumer
    : public IYsonConsumer
{
    virtual void OnStringScalar(const TStringBuf& value) override
    {
        Y_UNUSED(value);
    }

    virtual void OnInt64Scalar(i64 value) override
    {
        Y_UNUSED(value);
    }

    virtual void OnUint64Scalar(ui64 value) override
    {
        Y_UNUSED(value);
    }

    virtual void OnDoubleScalar(double value) override
    {
        Y_UNUSED(value);
    }

    virtual void OnBooleanScalar(bool value) override
    {
        Y_UNUSED(value);
    }

    virtual void OnEntity() override
    { }

    virtual void OnBeginList() override
    { }

    virtual void OnListItem() override
    { }

    virtual void OnEndList() override
    { }

    virtual void OnBeginMap() override
    { }

    virtual void OnKeyedItem(const TStringBuf& name) override
    {
        Y_UNUSED(name);
    }

    virtual void OnEndMap() override
    { }

    virtual void OnBeginAttributes() override
    { }

    virtual void OnEndAttributes() override
    { }

    virtual void OnRaw(const TStringBuf& yson, EYsonType type)
    {
        Y_UNUSED(yson);
        Y_UNUSED(type);
    }
};

IYsonConsumer* GetNullYsonConsumer()
{
    return Singleton<TNullYsonConsumer>();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
