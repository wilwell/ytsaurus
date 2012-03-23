#include "stdafx.h"
#include "node_detail.h"
#include "ypath_detail.h"
#include "ypath_service.h"
#include "tree_visitor.h"
#include "tree_builder.h"
#include "yson_writer.h"
#include "ypath_client.h"
#include "serialize.h"

namespace NYT {
namespace NYTree {

using namespace NRpc;

////////////////////////////////////////////////////////////////////////////////

bool TNodeBase::IsWriteRequest(IServiceContext* context) const
{
    DECLARE_YPATH_SERVICE_WRITE_METHOD(Set);
    DECLARE_YPATH_SERVICE_WRITE_METHOD(SetNode);
    DECLARE_YPATH_SERVICE_WRITE_METHOD(Remove);
    return TYPathServiceBase::IsWriteRequest(context);
}

void TNodeBase::DoInvoke(IServiceContext* context)
{
    DISPATCH_YPATH_SERVICE_METHOD(Get);
    DISPATCH_YPATH_SERVICE_METHOD(Set);
    DISPATCH_YPATH_SERVICE_METHOD(Remove);
    DISPATCH_YPATH_SERVICE_METHOD(GetNode);
    DISPATCH_YPATH_SERVICE_METHOD(SetNode);
    DISPATCH_YPATH_SERVICE_METHOD(List);
    TYPathServiceBase::DoInvoke(context);
}

void TNodeBase::GetSelf(TReqGet* request, TRspGet* response, TCtxGet* context)
{
    UNUSED(request);
    
    TStringStream stream;
    TYsonWriter writer(&stream);
    VisitTree(this, &writer, false);

    response->set_value(stream.Str());
    context->Reply();
}

void TNodeBase::GetNodeSelf(TReqGetNode* request, TRspGetNode* response, TCtxGetNode* context)
{
    UNUSED(request);

    response->set_value_ptr(reinterpret_cast<i64>(static_cast<INode*>(this)));
    context->Reply();
}

void TNodeBase::SetNodeSelf(TReqSetNode* request, TRspSetNode* response, TCtxSetNode* context)
{
    UNUSED(response);

    auto parent = GetParent();
    if (!parent) {
        ythrow yexception() << "Cannot replace the root";
    }

    auto value = reinterpret_cast<INode*>(request->value_ptr());
    parent->ReplaceChild(this, value);
    context->Reply();
}

void TNodeBase::RemoveSelf(TReqRemove* request, TRspRemove* response, TCtxRemove* context)
{
    UNUSED(request);
    UNUSED(response);

    auto parent = GetParent();
    if (!parent) {
        ythrow yexception() << "Cannot remove the root";
    }

    parent->AsComposite()->RemoveChild(this);
    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

void TMapNodeMixin::ListSelf(TReqList* request, TRspList* response, TCtxList* context)
{
    UNUSED(request);

    NYT::ToProto(response->mutable_keys(), GetKeys());
    context->Reply();
}

IYPathService::TResolveResult TMapNodeMixin::ResolveRecursive(
    const TYPath& path,
    const Stroka& verb)
{
    Stroka token;
    TYPath suffixPath;
    ChopYPathToken(path, &token, &suffixPath);

    auto child = FindChild(token);
    if (child) {
        return IYPathService::TResolveResult::There(~child, suffixPath);
    }

    if (verb == "Set" || verb == "SetNode" || verb == "Create") {
        return IYPathService::TResolveResult::Here(path);
    }

    ythrow yexception() << Sprintf("Key %s is not found", ~token.Quote());
}

void TMapNodeMixin::SetRecursive(
    INodeFactory* factory,
    const TYPath& path,
    NProto::TReqSet* request)
{
    auto value = DeserializeFromYson(request->value(), factory);
    TMapNodeMixin::SetRecursive(factory, path, ~value);
}

void TMapNodeMixin::SetRecursive(
    INodeFactory* factory,
    const TYPath& path,
    INode* value)
{
    // Split path into tokens.
    // Check that no attribute markers are present.
    std::vector<Stroka> tokens;
    TYPath currentPath = path;
    while (!currentPath.empty()) {
        if (IsAttributeYPath(currentPath)) {
            ythrow yexception() << Sprintf("Cannot set an attribute of a non-existing node");
        }

        Stroka token;
        TYPath suffixPath;
        ChopYPathToken(currentPath, &token, &suffixPath);

        tokens.push_back(token);
        currentPath = suffixPath;
    }

    // Check that the first token gives a unique key.
    auto firstToken = tokens.front();
    if (FindChild(firstToken)) {
        ythrow yexception() << Sprintf("Key %s already exists", ~firstToken.Quote());
    }

    // Make the actual changes.
    IMapNodePtr currentNode = this;
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        auto token = *it;
        if (it == tokens.end() - 1) {
            // Final step: append the given value.
            // AppendChild may throw an exception, e.g. in case of a failed lock attempt.
            bool appended = currentNode->AddChild(value, token);
            YVERIFY(appended);
        } else {
            // Intermediate step: create and append a map.
            auto intermediateNode = factory->CreateMap();
            // AppendChild may throw an exception, e.g. in case of a failed lock attempt.
            bool appended  = currentNode->AddChild(~intermediateNode, token);
            YVERIFY(appended);
            currentNode = intermediateNode;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

IYPathService::TResolveResult TListNodeMixin::ResolveRecursive(
    const TYPath& path,
    const Stroka& verb)
{
    Stroka token;
    TYPath suffixPath;
    ChopYPathToken(path, &token, &suffixPath);

    if (token.empty()) {
        ythrow yexception() << "Child index is empty";
    }

    char lastPrefixCh = token[token.length() - 1];
    if ((verb == "Set" || verb == "SetNode" || verb == "Create") &&
        (lastPrefixCh == '+' || lastPrefixCh == '-'))
    {
        return IYPathService::TResolveResult::Here(path);
    } else {
        int index = ParseChildIndex(token);
        auto child = FindChild(index);
        YASSERT(child);
        return IYPathService::TResolveResult::There(~child, suffixPath);
    }
}

void TListNodeMixin::SetRecursive(
    INodeFactory* factory,
    const TYPath& path,
    NProto::TReqSet* request)
{
    auto value = DeserializeFromYson(request->value(), factory);
    SetRecursive(factory, path, ~value);
}

void TListNodeMixin::SetRecursive(
    INodeFactory* factory,
    const TYPath& path,
    INode* value)
{
    INodePtr currentNode = this;
    TYPath currentPath = path;

    Stroka token;
    TYPath suffixPath;
    ChopYPathToken(currentPath, &token, &suffixPath);

    if (token.empty()) {
        ythrow yexception() << "Child index is empty";
    }

    if (token == "+") {
        return CreateChild(factory, GetChildCount(), suffixPath, value);
    } else if (token == "-") {
        return CreateChild(factory, 0, suffixPath, value);
    }

    char lastPrefixCh = token[token.length() - 1];
    if (lastPrefixCh != '+' && lastPrefixCh != '-') {
        ythrow yexception() << "Insertion point expected";
    }

    int index = ParseChildIndex(TStringBuf(token.begin(), token.length() - 1));
    switch (lastPrefixCh) {
        case '+':
            CreateChild(factory, index + 1, suffixPath, value);
            break;
        case '-':
            CreateChild(factory, index, suffixPath, value);
            break;

        default:
            YUNREACHABLE();
    }
}

void TListNodeMixin::CreateChild(
    INodeFactory* factory,
    int beforeIndex,
    const TYPath& path,
    INode* value)
{
    if (IsFinalYPath(path)) {
        AddChild(value, beforeIndex);
    } else {
        auto currentNode = factory->CreateMap();
        auto currentPath = path;
        AddChild(~currentNode, beforeIndex);

        while (true) {
            Stroka token;
            TYPath suffixPath;
            ChopYPathToken(currentPath, &token, &suffixPath);

            if (IsFinalYPath(suffixPath)) {
                YVERIFY(currentNode->AddChild(value, token));
                break;
            }

            auto intermediateNode = factory->CreateMap();
            YVERIFY(currentNode->AddChild(~intermediateNode, token));

            currentNode = intermediateNode;
            currentPath = suffixPath;
        }
    }
}

int TListNodeMixin::ParseChildIndex(const TStringBuf& str)
{
    int index;
    try {
        index = FromString<int>(str);
    } catch (const std::exception& ex) {
        ythrow yexception() << Sprintf("Failed to parse index %s\n%s",
            ~Stroka(str).Quote(),
            ex.what());
    }

    int count = GetChildCount();
    if (count == 0) {
        ythrow yexception() << Sprintf("Invalid index %d: list is empty",
            index);
    }

    if (index < 0 || index >= count) {
        ythrow yexception() << Sprintf("Invalid index %d: expected value in range %d..%d",
            index,
            0,
            count - 1);
    }

    return index;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT

