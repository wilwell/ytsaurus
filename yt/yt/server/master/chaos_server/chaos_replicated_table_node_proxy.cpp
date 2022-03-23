#include "chaos_replicated_table_node_proxy.h"

#include "chaos_replicated_table_node.h"
#include "chaos_cell_bundle.h"
#include "chaos_manager.h"

#include <yt/yt/server/master/cell_master/config_manager.h>
#include <yt/yt/server/master/cell_master/hydra_facade.h>

#include <yt/yt/server/master/cypress_server/node_proxy_detail.h>

#include <yt/yt/server/master/security_server/access_log.h>
#include <yt/yt/server/master/security_server/security_manager.h>

#include <yt/yt/server/master/table_server/table_manager.h>

#include <yt/yt/server/lib/misc/interned_attributes.h>

#include <yt/yt/ytlib/api/native/connection.h>
#include <yt/yt/ytlib/api/native/client.h>

#include <yt/yt/ytlib/table_client/schema.h>
#include <yt/yt/ytlib/table_client/table_ypath_proxy.h>

#include <yt/yt/client/chaos_client/replication_card.h>
#include <yt/yt/client/chaos_client/replication_card_serialization.h>

#include <yt/yt/core/rpc/authentication_identity.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NChaosServer {

using namespace NApi::NNative;
using namespace NApi;
using namespace NCellMaster;
using namespace NChaosClient;
using namespace NCypressServer;
using namespace NObjectServer;
using namespace NRpc;
using namespace NSecurityServer;
using namespace NTableClient;
using namespace NTransactionServer;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

class TChaosReplicatedTableNodeProxy
    : public TCypressNodeProxyBase<TNontemplateCypressNodeProxyBase, IEntityNode, TChaosReplicatedTableNode>
{
public:
    YTREE_NODE_TYPE_OVERRIDES_WITH_CHECK(Entity)

public:
    using TCypressNodeProxyBase::TCypressNodeProxyBase;

private:
    using TBase = TCypressNodeProxyBase<TNontemplateCypressNodeProxyBase, IEntityNode, TChaosReplicatedTableNode>;

    void ListSystemAttributes(std::vector<TAttributeDescriptor>* descriptors) override
    {
        TBase::ListSystemAttributes(descriptors);

        const auto* impl = GetThisImpl();

        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::ChaosCellBundle)
            .SetWritable(true)
            .SetReplicated(true)
            .SetPresent(impl->ChaosCellBundle().IsAlive()));
        descriptors->push_back(EInternedAttributeKey::Dynamic);
        descriptors->push_back(EInternedAttributeKey::ReplicationCardId);
        descriptors->push_back(EInternedAttributeKey::OwnsReplicationCard);
        descriptors->push_back(EInternedAttributeKey::Era);
        descriptors->push_back(EInternedAttributeKey::CoordinatorCellIds);
        descriptors->push_back(EInternedAttributeKey::Replicas);
        descriptors->push_back(TAttributeDescriptor(EInternedAttributeKey::Schema)
            .SetWritable(true)
            .SetReplicated(true));
    }

    bool GetBuiltinAttribute(TInternedAttributeKey key, NYson::IYsonConsumer* consumer) override
    {
        const auto* node = GetThisImpl();
        const auto* trunkNode = node->GetTrunkNode();

        switch (key) {
            case EInternedAttributeKey::ChaosCellBundle:
                if (const auto& bundle = trunkNode->ChaosCellBundle()) {
                    BuildYsonFluently(consumer)
                        .Value(bundle->GetName());
                    return true;
                } else {
                    return false;
                }

            case EInternedAttributeKey::Dynamic:
                BuildYsonFluently(consumer)
                    .Value(true);
                return true;

            case EInternedAttributeKey::ReplicationCardId:
                BuildYsonFluently(consumer)
                    .Value(node->GetReplicationCardId());
                return true;

            case EInternedAttributeKey::OwnsReplicationCard:
                BuildYsonFluently(consumer)
                    .Value(node->GetOwnsReplicationCard());
                return true;

            default:
                break;
        }

        return TCypressNodeProxyBase::GetBuiltinAttribute(key, consumer);
    }

    bool SetBuiltinAttribute(TInternedAttributeKey key, const TYsonString& value) override
    {
        switch (key) {
            case EInternedAttributeKey::ChaosCellBundle: {
                ValidateNoTransaction();

                auto name = ConvertTo<TString>(value);

                const auto& chaosManager = Bootstrap_->GetChaosManager();
                auto* cellBundle = chaosManager->GetChaosCellBundleByNameOrThrow(name, true /*activeLifeStageOnly*/);

                auto* lockedImpl = LockThisImpl();
                chaosManager->SetChaosCellBundle(lockedImpl, cellBundle);

                return true;
            }

            case EInternedAttributeKey::OwnsReplicationCard: {
                ValidateNoTransaction();
                auto* lockedImpl = LockThisImpl();
                lockedImpl->SetOwnsReplicationCard(ConvertTo<bool>(value));
                return true;
            }

            default:
                break;
        }

        return TCypressNodeProxyBase::SetBuiltinAttribute(key, value);
    }

    TFuture<TYsonString> GetBuiltinAttributeAsync(TInternedAttributeKey key) override
    {
        const auto* table = GetThisImpl();

        switch (key) {
            case EInternedAttributeKey::Era:
                return GetReplicationCard()
                    .Apply(BIND([] (const TReplicationCardPtr& card) {
                        return BuildYsonStringFluently()
                            .Value(card->Era);
                    }));

            case EInternedAttributeKey::CoordinatorCellIds:
                return GetReplicationCard({.IncludeCoordinators = true})
                    .Apply(BIND([] (const TReplicationCardPtr& card) {
                        return BuildYsonStringFluently()
                            .Value(card->CoordinatorCellIds);
                    }));

            case EInternedAttributeKey::Replicas:
                return GetReplicationCard()
                    .Apply(BIND([] (const TReplicationCardPtr& card) {
                        return BuildYsonStringFluently()
                            .Value(card->Replicas);
                    }));

            case EInternedAttributeKey::Schema:
                if (!table->GetSchema()) {
                    break;
                }
                return table->GetSchema()->AsYsonAsync();


            default:
                break;
        }

        return TCypressNodeProxyBase::GetBuiltinAttributeAsync(key);
    }

    bool DoInvoke(const IServiceContextPtr& context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(GetMountInfo);
        DISPATCH_YPATH_SERVICE_METHOD(Alter);
        return TBase::DoInvoke(context);
    }

    TFuture<TReplicationCardPtr> GetReplicationCard(const TReplicationCardFetchOptions& options = {})
    {
        const auto& connection = Bootstrap_->GetClusterConnection();
        auto clientOptions = TClientOptions::FromAuthenticationIdentity(NRpc::GetCurrentAuthenticationIdentity());
        auto client = connection->CreateClient(clientOptions);
        const auto* impl = GetThisImpl();
        TGetReplicationCardOptions getCardOptions;
        static_cast<TReplicationCardFetchOptions&>(getCardOptions) = options;
        getCardOptions.BypassCache = true;
        return client->GetReplicationCard(impl->GetReplicationCardId(), getCardOptions)
            .Apply(BIND([client] (const TReplicationCardPtr& card) {
                return card;
            }));
    }

    DECLARE_YPATH_SERVICE_METHOD(NTableClient::NProto, GetMountInfo);
    DECLARE_YPATH_SERVICE_METHOD(NTableClient::NProto, Alter);
};

DEFINE_YPATH_SERVICE_METHOD(TChaosReplicatedTableNodeProxy, GetMountInfo)
{
    DeclareNonMutating();
    SuppressAccessTracking();

    context->SetRequestInfo();

    ValidateNotExternal();
    ValidateNoTransaction();

    const auto* trunkTable = GetThisImpl();

    if (!trunkTable->GetSchema()) {
        THROW_ERROR_EXCEPTION("Table schema is not specified");
    }
    if (!trunkTable->GetReplicationCardId()) {
        THROW_ERROR_EXCEPTION("Replication card id is not specified");
    }

    ToProto(response->mutable_table_id(), trunkTable->GetId());
    ToProto(response->mutable_upstream_replica_id(), NTabletClient::TTableReplicaId());
    ToProto(response->mutable_replication_card_id(), trunkTable->GetReplicationCardId());
    response->set_dynamic(true);
    ToProto(response->mutable_schema(), *trunkTable->GetSchema()->AsTableSchema());

    context->Reply();
}

DEFINE_YPATH_SERVICE_METHOD(TChaosReplicatedTableNodeProxy, Alter)
{
    DeclareMutating();

    NTableClient::TTableSchemaPtr schema;

    if (request->has_schema()) {
        schema = New<TTableSchema>(FromProto<TTableSchema>(request->schema()));
    }
    if (request->has_dynamic() ||
        request->has_upstream_replica_id() ||
        request->has_schema_modification() ||
        request->has_replication_progress())
    {
        THROW_ERROR_EXCEPTION("Chaos replicated table could not be altered in this way");
    }

    context->SetRequestInfo("Schema: %v",
        schema);

    auto* table = LockThisImpl();

    // NB: Sorted dynamic tables contain unique keys, set this for user.
    if (schema && schema->IsSorted() && !schema->GetUniqueKeys()) {
        schema = schema->ToUniqueKeys();
    }

    if (schema) {
        if (table->GetSchema()) {
            ValidateTableSchemaUpdate(
                *table->GetSchema()->AsTableSchema(),
                *schema,
                /*isTableDynamic*/ true,
                /*isTableEmpty*/ false);
        }

        const auto& config = Bootstrap_->GetConfigManager()->GetConfig();

        if (!config->EnableDescendingSortOrder || !config->EnableDescendingSortOrderDynamic) {
            ValidateNoDescendingSortOrder(*schema);
        }
    }

    YT_LOG_ACCESS(
        context,
        GetId(),
        GetPath(),
        Transaction_);

    if (schema) {
        const auto& tableManager = Bootstrap_->GetTableManager();
        tableManager->GetOrCreateMasterTableSchema(*schema, table);
    }

    context->Reply();
}

////////////////////////////////////////////////////////////////////////////////

ICypressNodeProxyPtr CreateChaosReplicatedTableNodeProxy(
    TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTransaction* transaction,
    TChaosReplicatedTableNode* trunkNode)
{
    return New<TChaosReplicatedTableNodeProxy>(
        bootstrap,
        metadata,
        transaction,
        trunkNode);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NChaosServer
