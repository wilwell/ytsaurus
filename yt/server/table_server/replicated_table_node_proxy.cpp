#include "table_node_proxy.h"
#include "table_node_proxy_detail.h"

namespace NYT {
namespace NTableServer {

using namespace NObjectServer;
using namespace NCypressServer;
using namespace NTransactionServer;

////////////////////////////////////////////////////////////////////////////////

ICypressNodeProxyPtr CreateReplicatedTableNodeProxy(
    NCellMaster::TBootstrap* bootstrap,
    TObjectTypeMetadata* metadata,
    TTransaction* transaction,
    TTableNode* trunkNode)
{
    return New<TReplicatedTableNodeProxy>(
        bootstrap,
        metadata,
        transaction,
        trunkNode);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NTableServer
} // namespace NYT


