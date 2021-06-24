#include "account_resource_usage_lease_proxy.h"
#include "account_resource_usage_lease.h"
#include "account.h"
#include "helpers.h"
#include "private.h"

#include <yt/yt/server/master/object_server/object_detail.h>

#include <yt/yt/server/master/transaction_server/transaction.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NSecurityServer {

using namespace NYson;
using namespace NYTree;
using namespace NObjectServer;

using NYT::ToProto;
using NYT::FromProto;

////////////////////////////////////////////////////////////////////////////////

class TAccountResourceUsageLeaseProxy
    : public TNonversionedObjectProxyBase<TAccountResourceUsageLease>
{
public:
    TAccountResourceUsageLeaseProxy(
        NCellMaster::TBootstrap* bootstrap,
        TObjectTypeMetadata* metadata,
        TAccountResourceUsageLease* accountResourceUsageLease)
        : TBase(bootstrap, metadata, accountResourceUsageLease)
    { }

private:
    typedef TNonversionedObjectProxyBase<TAccountResourceUsageLease> TBase;

    virtual void ValidateRemoval() override
    {
        ValidatePermission(EPermissionCheckScope::This, EPermission::Remove);
    }

    virtual void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        descriptors->push_back(EInternedAttributeKey::Account);
        descriptors->push_back(EInternedAttributeKey::TransactionId);
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ResourceUsage)
            .SetWritable(true));
        descriptors->push_back(EInternedAttributeKey::CreationTime);
    }

    virtual bool GetBuiltinAttribute(TInternedAttributeKey key, IYsonConsumer* consumer) override
    {
        const auto& chunkManager = Bootstrap_->GetChunkManager();
        const auto* accountResourceUsageLease = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::Account:
                BuildYsonFluently(consumer)
                    .Value(accountResourceUsageLease->GetAccount()->GetName());
                return true;

            case EInternedAttributeKey::TransactionId:
                BuildYsonFluently(consumer)
                    .Value(accountResourceUsageLease->GetTransaction()->GetId());
                return true;

            case EInternedAttributeKey::ResourceUsage:
                SerializeClusterResources(
                    chunkManager,
                    accountResourceUsageLease->Resources(),
                    accountResourceUsageLease->GetAccount(),
                    consumer);
                return true;

            case EInternedAttributeKey::CreationTime:
                BuildYsonFluently(consumer)
                    .Value(accountResourceUsageLease->GetCreationTime());
                return true;

            default:
                break;
        }

        return TBase::GetBuiltinAttribute(key, consumer);
    }
    
    virtual bool SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value) override
    {
        auto* accountResourceUsageLease = GetThisImpl();
        const auto& chunkManager = Bootstrap_->GetChunkManager();
        const auto& securityManager = Bootstrap_->GetSecurityManager();

        switch (key) {
            case EInternedAttributeKey::ResourceUsage: {
                auto serializableResources = ConvertTo<TSerializableClusterResourcesPtr>(value);
                auto resources = serializableResources->ToClusterResources(chunkManager);
                securityManager->UpdateAccountResourceUsageLease(accountResourceUsageLease, resources);
                return true;
            }

            default:
                break;
        }

        return TBase::SetBuiltinAttribute(key, value);
    }
};

IObjectProxyPtr CreateAccountResourceUsageLeaseProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TAccountResourceUsageLease* accountResourceUsageLease)
{
    return New<TAccountResourceUsageLeaseProxy>(bootstrap, metadata, accountResourceUsageLease);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NSecurityServer

