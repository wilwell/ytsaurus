#include "stdafx.h"
#include "cypress_integration.h"
#include "tablet_cell.h"
#include "tablet.h"
#include "tablet_manager.h"

#include <server/cell_master/bootstrap.h>

#include <server/cypress_server/virtual.h>
#include <server/cypress_server/node_detail.h>
#include <server/cypress_server/node_proxy_detail.h>

#include <server/tablet_server/tablet_manager.h>

namespace NYT {
namespace NTabletServer {

using namespace NYPath;
using namespace NRpc;
using namespace NYson;
using namespace NYTree;
using namespace NObjectClient;
using namespace NCypressServer;
using namespace NTransactionServer;
using namespace NObjectServer;
using namespace NCellMaster;

////////////////////////////////////////////////////////////////////////////////

class TTabletCellNodeProxy
    : public TMapNodeProxy
{
public:
    TTabletCellNodeProxy(
        INodeTypeHandlerPtr typeHandler,
        TBootstrap* bootstrap,
        TTransaction* transaction,
        TMapNode* trunkNode)
        : TMapNodeProxy(
            typeHandler,
            bootstrap,
            transaction,
            trunkNode)
    { }

    virtual TResolveResult ResolveSelf(const TYPath& path, IServiceContextPtr context) override
    {
        const auto& method = context->GetMethod();
        if (method == "Remove") {
            return TResolveResult::There(GetTargetProxy(), path);
        } else {
            return TMapNodeProxy::ResolveSelf(path, context);
        }
    }

    virtual IYPathService::TResolveResult ResolveAttributes(
        const TYPath& path,
        IServiceContextPtr /*context*/) override
    {
        return TResolveResult::There(
            GetTargetProxy(),
            "/@" + path);
    }

    virtual void WriteAttributesFragment(
        IYsonConsumer* consumer,
        const TAttributeFilter& filter,
        bool sortKeys) override
    {
        GetTargetProxy()->WriteAttributesFragment(consumer, filter, sortKeys);
    }

private:
    IObjectProxyPtr GetTargetProxy() const
    {
        auto key = GetParent()->AsMap()->GetChildKey(this);
        auto id = TTabletCellId::FromString(key);
     
        auto tabletManager = Bootstrap_->GetTabletManager();
        auto* cell = tabletManager->GetTabletCellOrThrow(id);

        auto objectManager = Bootstrap_->GetObjectManager();
        return objectManager->GetProxy(cell, nullptr);
    }

};

////////////////////////////////////////////////////////////////////////////////

class TTabletCellNodeTypeHandler
    : public TMapNodeTypeHandler
{
public:
    explicit TTabletCellNodeTypeHandler(TBootstrap* bootstrap)
        : TMapNodeTypeHandler(bootstrap)
    { }

    virtual EObjectType GetObjectType() override
    {
        return EObjectType::TabletCellNode;
    }

private:
    virtual ICypressNodeProxyPtr DoGetProxy(
        TMapNode* trunkNode,
        TTransaction* transaction) override
    {
        return New<TTabletCellNodeProxy>(
            this,
            Bootstrap_,
            transaction,
            trunkNode);
    }

};

INodeTypeHandlerPtr CreateTabletCellNodeTypeHandler(TBootstrap* bootstrap)
{
    return New<TTabletCellNodeTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

class TVirtualTabletMap
    : public TVirtualMulticellMapBase
{
public:
    explicit TVirtualTabletMap(TBootstrap* bootstrap)
        : TVirtualMulticellMapBase(bootstrap)
    { }

private:
    virtual std::vector<TObjectId> GetKeys(i64 sizeLimit) const override
    {
        auto tabletManager = Bootstrap_->GetTabletManager();
        return ToObjectIds(GetValues(tabletManager->Tablets(), sizeLimit));
    }

    virtual bool IsValid(TObjectBase* object) const
    {
        return object->GetType() == EObjectType::Tablet;
    }

    virtual i64 GetSize() const override
    {
        auto tabletManager = Bootstrap_->GetTabletManager();
        return tabletManager->Tablets().GetSize();
    }


protected:
    virtual TYPath GetWellKnownPath() const override
    {
        return "//sys/tablets";
    }

};

INodeTypeHandlerPtr CreateTabletMapTypeHandler(TBootstrap* bootstrap)
{
    YCHECK(bootstrap);

    auto service = New<TVirtualTabletMap>(bootstrap);
    return CreateVirtualTypeHandler(
        bootstrap,
        EObjectType::TabletMap,
        service,
        EVirtualNodeOptions::RedirectSelf);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTabletServer
} // namespace NYT
