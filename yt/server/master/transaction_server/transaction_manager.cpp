#include "transaction_manager.h"
#include "private.h"
#include "config.h"
#include "transaction.h"
#include "transaction_proxy.h"

#include <yt/server/master/cell_master/automaton.h>
#include <yt/server/master/cell_master/bootstrap.h>
#include <yt/server/master/cell_master/hydra_facade.h>
#include <yt/server/master/cell_master/multicell_manager.h>
#include <yt/server/master/cell_master/config.h>
#include <yt/server/master/cell_master/config_manager.h>
#include <yt/server/master/cell_master/serialize.h>

#include <yt/server/master/cypress_server/cypress_manager.h>
#include <yt/server/master/cypress_server/node.h>

#include <yt/server/lib/hive/transaction_supervisor.h>
#include <yt/server/lib/hive/transaction_lease_tracker.h>
#include <yt/server/lib/hive/transaction_manager_detail.h>

#include <yt/server/lib/hydra/composite_automaton.h>
#include <yt/server/lib/hydra/mutation.h>

#include <yt/server/master/object_server/attribute_set.h>
#include <yt/server/master/object_server/object.h>
#include <yt/server/master/object_server/type_handler_detail.h>

#include <yt/server/master/security_server/account.h>
#include <yt/server/master/security_server/security_manager.h>
#include <yt/server/master/security_server/user.h>

#include <yt/server/master/transaction_server/proto/transaction_manager.pb.h>

#include <yt/client/object_client/helpers.h>

#include <yt/ytlib/transaction_client/transaction_service.pb.h>

#include <yt/core/concurrency/thread_affinity.h>

#include <yt/core/misc/id_generator.h>
#include <yt/core/misc/string.h>

#include <yt/core/ytree/attributes.h>
#include <yt/core/ytree/ephemeral_node_factory.h>
#include <yt/core/ytree/fluent.h>

namespace NYT::NTransactionServer {

using namespace NCellMaster;
using namespace NObjectClient;
using namespace NObjectClient::NProto;
using namespace NObjectServer;
using namespace NCypressServer;
using namespace NHydra;
using namespace NHiveClient;
using namespace NHiveServer;
using namespace NYTree;
using namespace NYson;
using namespace NConcurrency;
using namespace NCypressServer;
using namespace NTransactionClient;
using namespace NTransactionClient::NProto;
using namespace NSecurityServer;

////////////////////////////////////////////////////////////////////////////////

class TTransactionManager::TTransactionTypeHandler
    : public TObjectTypeHandlerWithMapBase<TTransaction>
{
public:
    TTransactionTypeHandler(
        TImpl* owner,
        EObjectType objectType);

    virtual ETypeFlags GetFlags() const override
    {
        return ETypeFlags::ReplicateAttributes;
    }

    virtual EObjectType GetType() const override
    {
        return ObjectType_;
    }

private:
    const EObjectType ObjectType_;


    virtual TCellTagList DoGetReplicationCellTags(const TTransaction* transaction) override
    {
        return transaction->ReplicatedToCellTags();
    }

    virtual TString DoGetName(const TTransaction* transaction) override
    {
        return Format("transaction %v", transaction->GetId());
    }

    virtual IObjectProxyPtr DoGetProxy(TTransaction* transaction, TTransaction* /*dummyTransaction*/) override
    {
        return CreateTransactionProxy(Bootstrap_, &Metadata_, transaction);
    }

    virtual TAccessControlDescriptor* DoFindAcd(TTransaction* transaction) override
    {
        return &transaction->Acd();
    }
};

////////////////////////////////////////////////////////////////////////////////

class TTransactionManager::TImpl
    : public TMasterAutomatonPart
    , public TTransactionManagerBase<TTransaction>
{
public:
    //! Raised when a new transaction is started.
    DEFINE_SIGNAL(void(TTransaction*), TransactionStarted);

    //! Raised when a transaction is committed.
    DEFINE_SIGNAL(void(TTransaction*), TransactionCommitted);

    //! Raised when a transaction is aborted.
    DEFINE_SIGNAL(void(TTransaction*), TransactionAborted);

    DEFINE_BYREF_RO_PROPERTY(THashSet<TTransaction*>, TopmostTransactions);

    DECLARE_ENTITY_MAP_ACCESSORS(Transaction, TTransaction);

public:
    explicit TImpl(TBootstrap* bootstrap)
        : TMasterAutomatonPart(bootstrap, NCellMaster::EAutomatonThreadQueue::TransactionManager)
        , LeaseTracker_(New<TTransactionLeaseTracker>(
            Bootstrap_->GetHydraFacade()->GetTransactionTrackerInvoker(),
            TransactionServerLogger))
    {
        VERIFY_INVOKER_THREAD_AFFINITY(Bootstrap_->GetHydraFacade()->GetAutomatonInvoker(NCellMaster::EAutomatonThreadQueue::Default), AutomatonThread);
        VERIFY_INVOKER_THREAD_AFFINITY(Bootstrap_->GetHydraFacade()->GetTransactionTrackerInvoker(), TrackerThread);

        Logger = TransactionServerLogger;

        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraStartTransaction, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraRegisterTransactionActions, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraPrepareTransactionCommit, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraCommitTransaction, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraAbortTransaction, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraRegisterExternalNestedTransaction, Unretained(this)));
        TCompositeAutomatonPart::RegisterMethod(BIND(&TImpl::HydraUnregisterExternalNestedTransaction, Unretained(this)));

        RegisterLoader(
            "TransactionManager.Keys",
            BIND(&TImpl::LoadKeys, Unretained(this)));
        RegisterLoader(
            "TransactionManager.Values",
            BIND(&TImpl::LoadValues, Unretained(this)));

        RegisterSaver(
            ESyncSerializationPriority::Keys,
            "TransactionManager.Keys",
            BIND(&TImpl::SaveKeys, Unretained(this)));
        RegisterSaver(
            ESyncSerializationPriority::Values,
            "TransactionManager.Values",
            BIND(&TImpl::SaveValues, Unretained(this)));
    }

    void Initialize()
    {
        const auto& objectManager = Bootstrap_->GetObjectManager();
        objectManager->RegisterHandler(New<TTransactionTypeHandler>(this, EObjectType::Transaction));
        objectManager->RegisterHandler(New<TTransactionTypeHandler>(this, EObjectType::NestedTransaction));

        if (Bootstrap_->IsPrimaryMaster()) {
            const auto& multicellManager = Bootstrap_->GetMulticellManager();
            multicellManager->SubscribeValidateSecondaryMasterRegistration(
                BIND(&TImpl::OnValidateSecondaryMasterRegistration, MakeWeak(this)));
        }
    }

    TTransaction* StartTransaction(
        TTransaction* parent,
        std::vector<TTransaction*> prerequisiteTransactions,
        const TCellTagList& replicatedToCellTags,
        const TCellTagList& replicateStartToCellTags,
        std::optional<TDuration> timeout,
        std::optional<TInstant> deadline,
        const std::optional<TString>& title,
        const IAttributeDictionary& attributes,
        TTransactionId hintId)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& dynamicConfig = GetDynamicConfig();

        if (parent) {
            if (parent->GetPersistentState() != ETransactionState::Active) {
                parent->ThrowInvalidState();
            }

            if (parent->GetDepth() >= dynamicConfig->MaxTransactionDepth) {
                THROW_ERROR_EXCEPTION(
                    NTransactionClient::EErrorCode::TransactionDepthLimitReached,
                    "Transaction depth limit reached")
                    << TErrorAttribute("limit", dynamicConfig->MaxTransactionDepth);
            }
        }

        const auto& objectManager = Bootstrap_->GetObjectManager();
        auto transactionId = objectManager->GenerateId(
            parent ? EObjectType::NestedTransaction : EObjectType::Transaction,
            hintId);

        auto transactionHolder = std::make_unique<TTransaction>(transactionId);
        auto* transaction = TransactionMap_.Insert(transactionId, std::move(transactionHolder));

        // Every active transaction has a fake reference to itself.
        YT_VERIFY(transaction->RefObject() == 1);

        if (parent) {
            transaction->SetParent(parent);
            transaction->SetDepth(parent->GetDepth() + 1);
            YT_VERIFY(parent->NestedNativeTransactions().insert(transaction).second);
            objectManager->RefObject(transaction);
        } else {
            YT_VERIFY(TopmostTransactions_.insert(transaction).second);
        }

        transaction->SetState(ETransactionState::Active);
        transaction->ReplicatedToCellTags() = replicatedToCellTags;

        transaction->PrerequisiteTransactions() = std::move(prerequisiteTransactions);
        for (auto* prerequisiteTransaction : transaction->PrerequisiteTransactions()) {
            // NB: Duplicates are fine; prerequisite transactions may be duplicated.
            prerequisiteTransaction->DependentTransactions().insert(transaction);
        }

        bool foreign = (CellTagFromId(transactionId) != Bootstrap_->GetCellTag());
        if (foreign) {
            transaction->SetForeign();
        }

        if (!foreign && timeout) {
            transaction->SetTimeout(std::min(*timeout, dynamicConfig->MaxTransactionTimeout));
        }

        transaction->SetDeadline(deadline);

        if (IsLeader()) {
            CreateLease(transaction);
        }

        transaction->SetTitle(title);

        // NB: This is not quite correct for replicated transactions but we don't care.
        const auto* mutationContext = GetCurrentMutationContext();
        transaction->SetStartTime(mutationContext->GetTimestamp());

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetAuthenticatedUser();
        transaction->Acd().SetOwner(user);

        objectManager->FillAttributes(transaction, attributes);

        TransactionStarted_.Fire(transaction);

        if (!replicateStartToCellTags.empty()) {
            NTransactionServer::NProto::TReqStartTransaction startRequest;
            startRequest.set_dont_replicate(true);
            ToProto(startRequest.mutable_attributes(), attributes);
            ToProto(startRequest.mutable_hint_id(), transactionId);
            if (parent) {
                ToProto(startRequest.mutable_parent_id(), parent->GetId());
            }
            if (timeout) {
                startRequest.set_timeout(ToProto<i64>(*timeout));
            }
            startRequest.set_user_name(user->GetName());
            if (title) {
                startRequest.set_title(*title);
            }

            const auto& multicellManager = Bootstrap_->GetMulticellManager();
            multicellManager->PostToMasters(startRequest, replicateStartToCellTags);
        }

        YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction started (TransactionId: %v, ParentId: %v, PrerequisiteTransactionIds: %v, "
            "ReplicateStartToCellTags: %v, ReplicatedToCellTags: %v, Timeout: %v, Deadline: %v, Title: %v)",
            transactionId,
            GetObjectId(parent),
            MakeFormattableView(transaction->PrerequisiteTransactions(), [] (auto* builder, const auto* prerequisiteTransaction) {
                FormatValue(builder, prerequisiteTransaction->GetId(), TStringBuf());
            }),
            replicateStartToCellTags,
            replicatedToCellTags,
            transaction->GetTimeout(),
            transaction->GetDeadline(),
            title);

        return transaction;
    }

    void CommitTransaction(
        TTransaction* transaction,
        TTimestamp commitTimestamp)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto transactionId = transaction->GetId();

        auto state = transaction->GetPersistentState();
        if (state == ETransactionState::Committed) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction is already committed (TransactionId: %v)",
                transactionId);
            return;
        }

        if (state != ETransactionState::Active &&
            state != ETransactionState::PersistentCommitPrepared)
        {
            transaction->ThrowInvalidState();
        }

        SetTimestampHolderTimestamp(transaction->GetId(), commitTimestamp);

        if (!transaction->ReplicatedToCellTags().empty()) {
            NProto::TReqCommitTransaction request;
            ToProto(request.mutable_transaction_id(), transactionId);
            request.set_commit_timestamp(commitTimestamp);

            const auto& multicellManager = Bootstrap_->GetMulticellManager();
            multicellManager->PostToMasters(request, transaction->ReplicatedToCellTags());
        }

        SmallVector<TTransaction*, 16> nestedNativeTransactions(
            transaction->NestedNativeTransactions().begin(),
            transaction->NestedNativeTransactions().end());
        std::sort(nestedNativeTransactions.begin(), nestedNativeTransactions.end(), TObjectRefComparer::Compare);
        for (auto* nestedTransaction : nestedNativeTransactions) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(), "Aborting nested native transaction on parent commit (TransactionId: %v, ParentId: %v)",
                nestedTransaction->GetId(),
                transactionId);
            AbortTransaction(nestedTransaction, true);
        }
        YT_VERIFY(transaction->NestedNativeTransactions().empty());
        YT_VERIFY(transaction->NestedExternalTransactionIds().empty());

        if (IsLeader()) {
            CloseLease(transaction);
        }

        transaction->SetState(ETransactionState::Committed);

        TransactionCommitted_.Fire(transaction);

        RunCommitTransactionActions(transaction);

        auto* parent = transaction->GetParent();
        if (parent) {
            parent->ExportedObjects().insert(
                parent->ExportedObjects().end(),
                transaction->ExportedObjects().begin(),
                transaction->ExportedObjects().end());
            parent->ImportedObjects().insert(
                parent->ImportedObjects().end(),
                transaction->ImportedObjects().begin(),
                transaction->ImportedObjects().end());

            parent->RecomputeResourceUsage();
        } else {
            const auto& objectManager = Bootstrap_->GetObjectManager();
            for (auto* object : transaction->ImportedObjects()) {
                objectManager->UnrefObject(object);
            }
        }
        transaction->ExportedObjects().clear();
        transaction->ImportedObjects().clear();

        if (parent && transaction->GetUnregisterFromParentOnCommit()) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "Unregistering transaction from parent on commit "
                "(TransactionId: %v, ParentTransactionId: %v)",
                transactionId,
                parent->GetId());
            NProto::TReqUnregisterExternalNestedTransaction request;
            ToProto(request.mutable_transaction_id(), parent->GetId());
            ToProto(request.mutable_nested_transaction_id(), transactionId);
            const auto& multicellManager = Bootstrap_->GetMulticellManager();
            multicellManager->PostToMaster(request, CellTagFromId(parent->GetId()));
        }

        FinishTransaction(transaction);

        YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction committed (TransactionId: %v, CommitTimestamp: %llx)",
            transactionId,
            commitTimestamp);
    }

    void AbortTransaction(
        TTransaction* transaction,
        bool force,
        bool validatePermissions = true)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto state = transaction->GetPersistentState();
        if (state == ETransactionState::Aborted) {
            return;
        }

        if (state == ETransactionState::PersistentCommitPrepared && !force ||
            state == ETransactionState::Committed)
        {
            transaction->ThrowInvalidState();
        }

        if (validatePermissions) {
            const auto& securityManager = Bootstrap_->GetSecurityManager();
            securityManager->ValidatePermission(transaction, EPermission::Write);
        }

        auto transactionId = transaction->GetId();
        const auto& multicellManager = Bootstrap_->GetMulticellManager();

        if (!transaction->ReplicatedToCellTags().empty()) {
            NProto::TReqAbortTransaction request;
            ToProto(request.mutable_transaction_id(), transactionId);
            request.set_force(force);
            multicellManager->PostToMasters(request, transaction->ReplicatedToCellTags());
        }

        SmallVector<TTransaction*, 16> nestedNativeTransactions(
            transaction->NestedNativeTransactions().begin(),
            transaction->NestedNativeTransactions().end());
        std::sort(nestedNativeTransactions.begin(), nestedNativeTransactions.end(), TObjectRefComparer::Compare);
        for (auto* nestedTransaction : nestedNativeTransactions) {
            AbortTransaction(nestedTransaction, force, false);
        }
        YT_VERIFY(transaction->NestedNativeTransactions().empty());

        SmallVector<TTransactionId, 16> nestedExternalTransactionIds(
            transaction->NestedExternalTransactionIds().begin(),
            transaction->NestedExternalTransactionIds().end());
        std::sort(nestedExternalTransactionIds.begin(), nestedExternalTransactionIds.end());
        for (auto nestedTransactionId : nestedExternalTransactionIds) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(), "Aborting nested external transaction (TransactionId: %v)",
                nestedTransactionId);
            NProto::TReqAbortTransaction request;
            ToProto(request.mutable_transaction_id(), nestedTransactionId);
            multicellManager->PostToMaster(request, CellTagFromId(nestedTransactionId));
        }
        transaction->NestedExternalTransactionIds().clear();

        auto* parent = transaction->GetParent();
        if (parent && transaction->GetUnregisterFromParentOnAbort()) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "Unregistering transaction from parent on abort "
                "(TransactionId: %v, ParentTransactionId: %v)",
                transactionId,
                parent->GetId());
            NProto::TReqUnregisterExternalNestedTransaction request;
            ToProto(request.mutable_transaction_id(), parent->GetId());
            ToProto(request.mutable_nested_transaction_id(), transactionId);
            multicellManager->PostToMaster(request, CellTagFromId(parent->GetId()));
        }

        if (IsLeader()) {
            CloseLease(transaction);
        }

        transaction->SetState(ETransactionState::Aborted);

        TransactionAborted_.Fire(transaction);

        RunAbortTransactionActions(transaction);

        const auto& objectManager = Bootstrap_->GetObjectManager();
        for (const auto& entry : transaction->ExportedObjects()) {
            auto* object = entry.Object;
            objectManager->UnrefObject(object);
            const auto& handler = objectManager->GetHandler(object);
            handler->UnexportObject(object, entry.DestinationCellTag, 1);
        }
        for (auto* object : transaction->ImportedObjects()) {
            objectManager->UnrefObject(object);
            object->ImportUnrefObject();
        }
        transaction->ExportedObjects().clear();
        transaction->ImportedObjects().clear();

        FinishTransaction(transaction);

        YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction aborted (TransactionId: %v, Force: %v)",
            transactionId,
            force);
    }

    void RegisterTransactionAtParent(TTransaction* transaction)
    {
        auto* parent = transaction->GetParent();
        YT_ASSERT(parent && parent->IsForeign());

        YT_LOG_DEBUG_UNLESS(IsRecovery(), "Registering transaction at parent (TransactionId: %v, ParentTransactionId: %v)",
            transaction->GetId(),
            parent->GetId());

        NTransactionServer::NProto::TReqRegisterExternalNestedTransaction request;
        ToProto(request.mutable_transaction_id(), parent->GetId());
        ToProto(request.mutable_nested_transaction_id(), transaction->GetId());
        const auto& multicellManager = Bootstrap_->GetMulticellManager();
        multicellManager->PostToMaster(request, CellTagFromId(parent->GetId()));
    }

    TTransaction* GetTransactionOrThrow(TTransactionId transactionId)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = FindTransaction(transactionId);
        if (!IsObjectAlive(transaction)) {
            THROW_ERROR_EXCEPTION(
                NTransactionClient::EErrorCode::NoSuchTransaction,
                "No such transaction %v",
                transactionId);
        }
        return transaction;
    }

    TFuture<TInstant> GetLastPingTime(const TTransaction* transaction)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        return LeaseTracker_->GetLastPingTime(transaction->GetId());
    }

    void SetTransactionTimeout(
        TTransaction* transaction,
        TDuration timeout)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        transaction->SetTimeout(timeout);

        if (IsLeader()) {
            LeaseTracker_->SetTimeout(transaction->GetId(), timeout);
        }
    }

    void StageObject(TTransaction* transaction, TObject* object)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        YT_VERIFY(transaction->StagedObjects().insert(object).second);
        const auto& objectManager = Bootstrap_->GetObjectManager();
        objectManager->RefObject(object);
    }

    void UnstageObject(TTransaction* transaction, TObject* object, bool recursive)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& objectManager = Bootstrap_->GetObjectManager();
        const auto& handler = objectManager->GetHandler(object);
        handler->UnstageObject(object, recursive);

        if (transaction) {
            YT_VERIFY(transaction->StagedObjects().erase(object) == 1);
            objectManager->UnrefObject(object);
        }
    }

    void StageNode(TTransaction* transaction, TCypressNode* trunkNode)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);
        YT_ASSERT(trunkNode->IsTrunk());

        const auto& objectManager = Bootstrap_->GetObjectManager();
        transaction->StagedNodes().push_back(trunkNode);
        objectManager->RefObject(trunkNode);
    }

    void ImportObject(TTransaction* transaction, TObject* object)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        transaction->ImportedObjects().push_back(object);
        const auto& objectManager = Bootstrap_->GetObjectManager();
        objectManager->RefObject(object);
        object->ImportRefObject();
    }

    void ExportObject(TTransaction* transaction, TObject* object, TCellTag destinationCellTag)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        transaction->ExportedObjects().push_back({object, destinationCellTag});

        const auto& objectManager = Bootstrap_->GetObjectManager();
        objectManager->RefObject(object);

        const auto& handler = objectManager->GetHandler(object);
        handler->ExportObject(object, destinationCellTag);
    }


    std::unique_ptr<TMutation> CreateStartTransactionMutation(
        TCtxStartTransactionPtr context,
        const NTransactionServer::NProto::TReqStartTransaction& request)
    {
        return CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            std::move(context),
            request,
            &TImpl::HydraStartTransaction,
            this);
    }

    std::unique_ptr<TMutation> CreateRegisterTransactionActionsMutation(TCtxRegisterTransactionActionsPtr context)
    {
        return CreateMutation(
            Bootstrap_->GetHydraFacade()->GetHydraManager(),
            std::move(context),
            &TImpl::HydraRegisterTransactionActions,
            this);
    }


    // ITransactionManager implementation.
    void PrepareTransactionCommit(
        TTransactionId transactionId,
        bool persistent,
        TTimestamp prepareTimestamp)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);

        if (!transaction->NestedExternalTransactionIds().empty()) {
            THROW_ERROR_EXCEPTION(
                NTransactionClient::EErrorCode::NestedExternalTransactionExists,
                "Cannot commit transaction %v since it has nested external transaction(s) %v",
                transactionId,
                transaction->NestedExternalTransactionIds());
        }

        // Allow preparing transactions in Active and TransientCommitPrepared (for persistent mode) states.
        // This check applies not only to #transaction itself but also to all of its ancestors.
        {
            auto* currentTransaction = transaction;
            while (currentTransaction) {
                auto state = persistent ? currentTransaction->GetPersistentState() : currentTransaction->GetState();
                if (state != ETransactionState::Active) {
                    currentTransaction->ThrowInvalidState();
                }
                currentTransaction = currentTransaction->GetParent();
            }
        }

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        securityManager->ValidatePermission(transaction, EPermission::Write);

        auto oldState = persistent ? transaction->GetPersistentState() : transaction->GetState();
        if (oldState == ETransactionState::Active) {
            RunPrepareTransactionActions(transaction, persistent);

            transaction->SetState(persistent
                ? ETransactionState::PersistentCommitPrepared
                : ETransactionState::TransientCommitPrepared);

            YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction commit prepared (TransactionId: %v, Persistent: %v, PrepareTimestamp: %llx)",
                transactionId,
                persistent,
                prepareTimestamp);
        }

        if (persistent && !transaction->ReplicatedToCellTags().empty()) {
            NProto::TReqPrepareTransactionCommit request;
            ToProto(request.mutable_transaction_id(), transactionId);
            request.set_prepare_timestamp(prepareTimestamp);
            request.set_user_name(*securityManager->GetAuthenticatedUserName());

            const auto& multicellManager = Bootstrap_->GetMulticellManager();
            multicellManager->PostToMasters(request, transaction->ReplicatedToCellTags());
        }
    }

    void PrepareTransactionAbort(TTransactionId transactionId, bool force)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);
        auto state = transaction->GetState();
        if (state != ETransactionState::Active && !force) {
            transaction->ThrowInvalidState();
        }

        if (state == ETransactionState::Active) {
            transaction->SetState(ETransactionState::TransientAbortPrepared);

            YT_LOG_DEBUG("Transaction abort prepared (TransactionId: %v)",
                transactionId);
        }
    }

    void CommitTransaction(
        TTransactionId transactionId,
        TTimestamp commitTimestamp)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);
        CommitTransaction(transaction, commitTimestamp);
    }

    void AbortTransaction(
        TTransactionId transactionId,
        bool force)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = GetTransactionOrThrow(transactionId);
        AbortTransaction(transaction, force);
    }

    void PingTransaction(
        TTransactionId transactionId,
        bool pingAncestors)
    {
        VERIFY_THREAD_AFFINITY(TrackerThread);

        LeaseTracker_->PingTransaction(transactionId, pingAncestors);
    }

    void CreateOrRefTimestampHolder(TTransactionId transactionId)
    {
        if (auto it = TimestampHolderMap_.find(transactionId)) {
            ++it->second.RefCount;
        }
        TimestampHolderMap_.emplace(transactionId, TTimestampHolder{});
    }

    void SetTimestampHolderTimestamp(TTransactionId transactionId, TTimestamp timestamp)
    {
        if (auto it = TimestampHolderMap_.find(transactionId)) {
            it->second.Timestamp = timestamp;
        }
    }

    TTimestamp GetTimestampHolderTimestamp(TTransactionId transactionId)
    {
        if (auto it = TimestampHolderMap_.find(transactionId)) {
            return it->second.Timestamp;
        }
        return NullTimestamp;
    }

    void UnrefTimestampHolder(TTransactionId transactionId)
    {
        if (auto it = TimestampHolderMap_.find(transactionId)) {
            --it->second.RefCount;
            if (it->second.RefCount == 0) {
                TimestampHolderMap_.erase(it);
            }
        }
    }

private:
    struct TTimestampHolder
    {
        TTimestamp Timestamp = NullTimestamp;
        i64 RefCount = 1;

        void Persist(NCellMaster::TPersistenceContext& context)
        {
            using ::NYT::Persist;
            Persist(context, Timestamp);
            Persist(context, RefCount);
        }
    };

    friend class TTransactionTypeHandler;

    const TTransactionLeaseTrackerPtr LeaseTracker_;

    NHydra::TEntityMap<TTransaction> TransactionMap_;

    THashMap<TTransactionId, TTimestampHolder> TimestampHolderMap_;

    DECLARE_THREAD_AFFINITY_SLOT(AutomatonThread);
    DECLARE_THREAD_AFFINITY_SLOT(TrackerThread);


    void HydraStartTransaction(
        const TCtxStartTransactionPtr& context,
        NTransactionServer::NProto::TReqStartTransaction* request,
        NTransactionServer::NProto::TRspStartTransaction* response)
    {
        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetUserByNameOrThrow(request->user_name());
        TAuthenticatedUserGuard userGuard(securityManager, user);

        const auto& objectManager = Bootstrap_->GetObjectManager();
        auto* schema = objectManager->GetSchema(EObjectType::Transaction);
        securityManager->ValidatePermission(schema, user, EPermission::Create);

        auto hintId = FromProto<TTransactionId>(request->hint_id());

        auto parentId = FromProto<TTransactionId>(request->parent_id());
        auto* parent = parentId ? GetTransactionOrThrow(parentId) : nullptr;

        auto prerequisiteTransactionIds = FromProto<std::vector<TTransactionId>>(request->prerequisite_transaction_ids());
        std::vector<TTransaction*> prerequisiteTransactions;
        for (const auto& id : prerequisiteTransactionIds) {
            auto* prerequisiteTransaction = FindTransaction(id);
            if (!IsObjectAlive(prerequisiteTransaction)) {
                THROW_ERROR_EXCEPTION(NObjectClient::EErrorCode::PrerequisiteCheckFailed,
                    "Prerequisite check failed: transaction %v is missing",
                    id);
            }
            if (prerequisiteTransaction->GetPersistentState() != ETransactionState::Active) {
                THROW_ERROR_EXCEPTION(NObjectClient::EErrorCode::PrerequisiteCheckFailed,
                    "Prerequisite check failed: transaction %v is in %Qlv state",
                    id,
                    prerequisiteTransaction->GetState());
            }
            prerequisiteTransactions.push_back(prerequisiteTransaction);
        }

        auto attributes = request->has_attributes()
            ? FromProto(request->attributes())
            : CreateEphemeralAttributes();

        auto title = request->has_title() ? std::make_optional(request->title()) : std::nullopt;

        auto timeout = FromProto<TDuration>(request->timeout());

        std::optional<TInstant> deadline;
        if (request->has_deadline()) {
            deadline = FromProto<TInstant>(request->deadline());
        }

        TCellTagList replicateToCellTags;
        if (!request->dont_replicate())  {
            replicateToCellTags = FromProto<TCellTagList>(request->replicate_to_cell_tags());
            if (replicateToCellTags.empty() && Bootstrap_->IsPrimaryMaster()) {
                const auto& multicellManager = Bootstrap_->GetMulticellManager();
                replicateToCellTags = multicellManager->GetRegisteredMasterCellTags();
            }
        }

        auto* transaction = StartTransaction(
            parent,
            prerequisiteTransactions,
            replicateToCellTags,
            replicateToCellTags,
            timeout,
            deadline,
            title,
            *attributes,
            hintId);

        auto id = transaction->GetId();

        if (response) {
            ToProto(response->mutable_id(), id);
        }

        if (context) {
            context->SetResponseInfo("TransactionId: %v", id);
        }
    }

    void HydraRegisterTransactionActions(
        const TCtxRegisterTransactionActionsPtr& /*context*/,
        TReqRegisterTransactionActions* request,
        TRspRegisterTransactionActions* /*response*/)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());

        auto* transaction = GetTransactionOrThrow(transactionId);

        auto state = transaction->GetPersistentState();
        if (state != ETransactionState::Active) {
            transaction->ThrowInvalidState();
        }

        for (const auto& protoData : request->actions()) {
            auto data = FromProto<TTransactionActionData>(protoData);
            transaction->Actions().push_back(data);

            YT_LOG_DEBUG_UNLESS(IsRecovery(), "Transaction action registered (TransactionId: %v, ActionType: %v)",
                transactionId,
                data.Type);
        }
    }

    void HydraPrepareTransactionCommit(NProto::TReqPrepareTransactionCommit* request)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());
        auto prepareTimestamp = request->prepare_timestamp();
        auto userName = request->user_name();

        const auto& securityManager = Bootstrap_->GetSecurityManager();
        auto* user = securityManager->GetUserByNameOrThrow(userName);
        TAuthenticatedUserGuard(securityManager, user);

        PrepareTransactionCommit(transactionId, true, prepareTimestamp);
    }

    void HydraCommitTransaction(NProto::TReqCommitTransaction* request)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());
        auto commitTimestamp = request->commit_timestamp();
        CommitTransaction(transactionId, commitTimestamp);
    }

    void HydraAbortTransaction(NProto::TReqAbortTransaction* request)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());
        bool force = request->force();
        AbortTransaction(transactionId, force);
    }

    void HydraRegisterExternalNestedTransaction(NProto::TReqRegisterExternalNestedTransaction* request)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());
        auto nestedTransactionId = FromProto<TTransactionId>(request->nested_transaction_id());
        auto* transaction = FindTransaction(transactionId);
        if (!IsObjectAlive(transaction)) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "Requested to register an external nested transaction for a non-existing parent transaction; ignored "
                "(TransactionId: %v, NestedTransactionId: %v)",
                transactionId,
                nestedTransactionId);
            return;
        }

        auto* currentTransaction = transaction;
        while (currentTransaction) {
            if (currentTransaction->GetPersistentState() != ETransactionState::Active) {
                YT_LOG_DEBUG_UNLESS(IsRecovery(),
                    "Requested to register an external nested transaction for a non-active transaction; ignored "
                    "(TransactionId: %v, State: %v, NestedTransactionId: %v)",
                    currentTransaction->GetId(),
                    currentTransaction->GetPersistentState(),
                    nestedTransactionId);
                return;
            }
            currentTransaction = currentTransaction->GetParent();
        }

        if (transaction->NestedExternalTransactionIds().insert(nestedTransactionId).second) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "External nested transaction registered (TransactionId: %v, NestedTransactionId: %v)",
                transactionId,
                nestedTransactionId);
        } else {
            YT_LOG_ALERT_UNLESS(IsRecovery(),
                "External nested transaction re-registered (TransactionId: %v, NestedTransactionId: %v)",
                transaction,
                nestedTransactionId);
        }
    }

    void HydraUnregisterExternalNestedTransaction(NProto::TReqUnregisterExternalNestedTransaction* request)
    {
        auto transactionId = FromProto<TTransactionId>(request->transaction_id());
        auto nestedTransactionId = FromProto<TTransactionId>(request->nested_transaction_id());
        auto* transaction = FindTransaction(transactionId);
        if (!IsObjectAlive(transaction)) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "Requested to unregister an external nested transaction for a non-existing parent transaction; ignored "
                "(TransactionId: %v, NestedTransactionId: %v)",
                transactionId,
                nestedTransactionId);
            return;
        }

        if (transaction->NestedExternalTransactionIds().erase(nestedTransactionId)) {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "External nested transaction unregistered (TransactionId: %v, NestedTransactionId: %v)",
                transactionId,
                nestedTransactionId);
        } else {
            YT_LOG_DEBUG_UNLESS(IsRecovery(),
                "Requested to unregister a non-existing external nested transaction; ignored "
                "(TransactionId: %v, NestedTransactionId: %v)",
                transactionId,
                nestedTransactionId);
        }
    }

// COMPAT(shakurov)
public:
    void FinishTransaction(TTransaction* transaction)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        const auto& objectManager = Bootstrap_->GetObjectManager();

        for (auto* object : transaction->StagedObjects()) {
            const auto& handler = objectManager->GetHandler(object);
            handler->UnstageObject(object, false);
            objectManager->UnrefObject(object);
        }
        transaction->StagedObjects().clear();

        for (auto* node : transaction->StagedNodes()) {
            objectManager->UnrefObject(node);
        }
        transaction->StagedNodes().clear();

        auto* parent = transaction->GetParent();
        if (parent) {
            YT_VERIFY(parent->NestedNativeTransactions().erase(transaction) == 1);
            objectManager->UnrefObject(transaction);
            transaction->SetParent(nullptr);
        } else {
            YT_VERIFY(TopmostTransactions_.erase(transaction) == 1);
        }

        for (auto* prerequisiteTransaction : transaction->PrerequisiteTransactions()) {
            // NB: Duplicates are fine; prerequisite transactions may be duplicated.
            prerequisiteTransaction->DependentTransactions().erase(transaction);
        }
        transaction->PrerequisiteTransactions().clear();

        SmallVector<TTransaction*, 16> dependentTransactions(
            transaction->DependentTransactions().begin(),
            transaction->DependentTransactions().end());
        std::sort(dependentTransactions.begin(), dependentTransactions.end(), TObjectRefComparer::Compare);
        for (auto* dependentTransaction : dependentTransactions) {
            if (!IsObjectAlive(dependentTransaction)) {
                continue;
            }
            if (dependentTransaction->GetPersistentState() != ETransactionState::Active) {
                continue;
            }
            YT_LOG_DEBUG("Aborting dependent transaction (DependentTransactionId: %v, PrerequisiteTransactionId: %v)",
                dependentTransaction->GetId(),
                transaction->GetId());
            AbortTransaction(dependentTransaction, true, false);
        }
        transaction->DependentTransactions().clear();

        transaction->SetDeadline(std::nullopt);

        // Kill the fake reference thus destroying the object.
        objectManager->UnrefObject(transaction);
    }
private:

    void SaveKeys(NCellMaster::TSaveContext& context)
    {
        TransactionMap_.SaveKeys(context);
    }

    void SaveValues(NCellMaster::TSaveContext& context)
    {
        TransactionMap_.SaveValues(context);
        Save(context, TimestampHolderMap_);
    }

    void LoadKeys(NCellMaster::TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TransactionMap_.LoadKeys(context);
    }

    void LoadValues(NCellMaster::TLoadContext& context)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TransactionMap_.LoadValues(context);

        // COMPAT(savrus)
        if (context.GetVersion() >= EMasterReign::BulkInsert) {
            Load(context, TimestampHolderMap_);
        }
    }


    void OnAfterSnapshotLoaded()
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        // Reconstruct TopmostTransactions.
        TopmostTransactions_.clear();
        for (const auto& pair : TransactionMap_) {
            auto* transaction = pair.second;
            if (IsObjectAlive(transaction) && !transaction->GetParent()) {
                YT_VERIFY(TopmostTransactions_.insert(transaction).second);
            }
        }
    }

    virtual void Clear() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TMasterAutomatonPart::Clear();

        TransactionMap_.Clear();
        TopmostTransactions_.clear();
    }


    virtual void OnLeaderActive() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TMasterAutomatonPart::OnLeaderActive();

        for (const auto& pair : TransactionMap_) {
            auto* transaction = pair.second;
            if (transaction->GetState() == ETransactionState::Active ||
                transaction->GetState() == ETransactionState::PersistentCommitPrepared)
            {
                CreateLease(transaction);
            }
        }

        LeaseTracker_->Start();
    }

    virtual void OnStopLeading() override
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        TMasterAutomatonPart::OnStopLeading();

        LeaseTracker_->Stop();

        // Reset all transiently prepared transactions back into active state.
        for (const auto& pair : TransactionMap_) {
            auto* transaction = pair.second;
            transaction->SetState(transaction->GetPersistentState());
        }
    }


    void CreateLease(TTransaction* transaction)
    {
        const auto& hydraFacade = Bootstrap_->GetHydraFacade();
        LeaseTracker_->RegisterTransaction(
            transaction->GetId(),
            GetObjectId(transaction->GetParent()),
            transaction->GetTimeout(),
            transaction->GetDeadline(),
            BIND(&TImpl::OnTransactionExpired, MakeStrong(this))
                .Via(hydraFacade->GetEpochAutomatonInvoker(EAutomatonThreadQueue::TransactionSupervisor)));
    }

    void CloseLease(TTransaction* transaction)
    {
        LeaseTracker_->UnregisterTransaction(transaction->GetId());
    }

    void OnTransactionExpired(TTransactionId transactionId)
    {
        VERIFY_THREAD_AFFINITY(AutomatonThread);

        auto* transaction = FindTransaction(transactionId);
        if (!IsObjectAlive(transaction))
            return;
        if (transaction->GetState() != ETransactionState::Active)
            return;

        const auto& transactionSupervisor = Bootstrap_->GetTransactionSupervisor();
        transactionSupervisor->AbortTransaction(transactionId).Subscribe(BIND([=] (const TError& error) {
            if (!error.IsOK()) {
                YT_LOG_DEBUG(error, "Error aborting expired transaction (TransactionId: %v)",
                    transactionId);
            }
        }));
    }


    void OnValidateSecondaryMasterRegistration(TCellTag cellTag)
    {
        if (TransactionMap_.GetSize() > 0) {
            THROW_ERROR_EXCEPTION("Cannot register a new secondary master %v while %d transaction(s) are present",
                cellTag,
                TransactionMap_.GetSize());
        }
    }


    const TDynamicTransactionManagerConfigPtr& GetDynamicConfig()
    {
        return Bootstrap_->GetConfigManager()->GetConfig()->TransactionManager;
    }
};

DEFINE_ENTITY_MAP_ACCESSORS(TTransactionManager::TImpl, Transaction, TTransaction, TransactionMap_)

////////////////////////////////////////////////////////////////////////////////

TTransactionManager::TTransactionTypeHandler::TTransactionTypeHandler(
    TImpl* owner,
    EObjectType objectType)
    : TObjectTypeHandlerWithMapBase(owner->Bootstrap_, &owner->TransactionMap_)
    , ObjectType_(objectType)
{ }

////////////////////////////////////////////////////////////////////////////////

TTransactionManager::TTransactionManager(TBootstrap* bootstrap)
    : Impl_(New<TImpl>(bootstrap))
{ }

void TTransactionManager::Initialize()
{
    Impl_->Initialize();
}

TTransaction* TTransactionManager::StartTransaction(
    TTransaction* parent,
    std::vector<TTransaction*> prerequisiteTransactions,
    const TCellTagList& replicatedToCellTags,
    const TCellTagList& replicateStartToCellTags,
    std::optional<TDuration> timeout,
    std::optional<TInstant> deadline,
    const std::optional<TString>& title,
    const IAttributeDictionary& attributes,
    TTransactionId hintId)
{
    return Impl_->StartTransaction(
        parent,
        std::move(prerequisiteTransactions),
        replicatedToCellTags,
        replicateStartToCellTags,
        timeout,
        deadline,
        title,
        attributes,
        hintId);
}

void TTransactionManager::CommitTransaction(
    TTransaction* transaction,
    TTimestamp commitTimestamp)
{
    Impl_->CommitTransaction(transaction, commitTimestamp);
}

void TTransactionManager::AbortTransaction(
    TTransaction* transaction,
    bool force)
{
    Impl_->AbortTransaction(transaction, force);
}

void TTransactionManager::RegisterTransactionAtParent(TTransaction* transaction)
{
    Impl_->RegisterTransactionAtParent(transaction);
}

// COMPAT(shakurov)
void TTransactionManager::FinishTransaction(TTransaction* transaction)
{
    Impl_->FinishTransaction(transaction);
}

TTransaction* TTransactionManager::GetTransactionOrThrow(TTransactionId transactionId)
{
    return Impl_->GetTransactionOrThrow(transactionId);
}

TFuture<TInstant> TTransactionManager::GetLastPingTime(const TTransaction* transaction)
{
    return Impl_->GetLastPingTime(transaction);
}

void TTransactionManager::SetTransactionTimeout(
    TTransaction* transaction,
    TDuration timeout)
{
    Impl_->SetTransactionTimeout(transaction, timeout);
}

void TTransactionManager::StageObject(
    TTransaction* transaction,
    TObject* object)
{
    Impl_->StageObject(transaction, object);
}

void TTransactionManager::UnstageObject(
    TTransaction* transaction,
    TObject* object,
    bool recursive)
{
    Impl_->UnstageObject(transaction, object, recursive);
}

void TTransactionManager::StageNode(
    TTransaction* transaction,
    TCypressNode* trunkNode)
{
    Impl_->StageNode(transaction, trunkNode);
}

void TTransactionManager::ExportObject(
    TTransaction* transaction,
    TObject* object,
    TCellTag destinationCellTag)
{
    Impl_->ExportObject(transaction, object, destinationCellTag);
}

void TTransactionManager::ImportObject(
    TTransaction* transaction,
    TObject* object)
{
    Impl_->ImportObject(transaction, object);
}

void TTransactionManager::RegisterTransactionActionHandlers(
    const TTransactionPrepareActionHandlerDescriptor<TTransaction>& prepareActionDescriptor,
    const TTransactionCommitActionHandlerDescriptor<TTransaction>& commitActionDescriptor,
    const TTransactionAbortActionHandlerDescriptor<TTransaction>& abortActionDescriptor)
{
    Impl_->RegisterTransactionActionHandlers(
        prepareActionDescriptor,
        commitActionDescriptor,
        abortActionDescriptor);
}

std::unique_ptr<TMutation> TTransactionManager::CreateStartTransactionMutation(
    TCtxStartTransactionPtr context,
    const NTransactionServer::NProto::TReqStartTransaction& request)
{
    return Impl_->CreateStartTransactionMutation(std::move(context), request);
}

std::unique_ptr<TMutation> TTransactionManager::CreateRegisterTransactionActionsMutation(TCtxRegisterTransactionActionsPtr context)
{
    return Impl_->CreateRegisterTransactionActionsMutation(std::move(context));
}

void TTransactionManager::PrepareTransactionCommit(
    TTransactionId transactionId,
    bool persistent,
    TTimestamp prepareTimestamp)
{
    Impl_->PrepareTransactionCommit(transactionId, persistent, prepareTimestamp);
}

void TTransactionManager::PrepareTransactionAbort(
    TTransactionId transactionId,
    bool force)
{
    Impl_->PrepareTransactionAbort(transactionId, force);
}

void TTransactionManager::CommitTransaction(
    TTransactionId transactionId,
    TTimestamp commitTimestamp)
{
    Impl_->CommitTransaction(transactionId, commitTimestamp);
}

void TTransactionManager::AbortTransaction(
    TTransactionId transactionId,
    bool force)
{
    Impl_->AbortTransaction(transactionId, force);
}

void TTransactionManager::PingTransaction(
    TTransactionId transactionId,
    bool pingAncestors)
{
    Impl_->PingTransaction(transactionId, pingAncestors);
}

void TTransactionManager::CreateOrRefTimestampHolder(TTransactionId transactionId)
{
    Impl_->CreateOrRefTimestampHolder(transactionId);
}

void TTransactionManager::SetTimestampHolderTimestamp(TTransactionId transactionId, TTimestamp timestamp)
{
    Impl_->SetTimestampHolderTimestamp(transactionId, timestamp);
}

TTimestamp TTransactionManager::GetTimestampHolderTimestamp(TTransactionId transactionId)
{
    return Impl_->GetTimestampHolderTimestamp(transactionId);
}

void TTransactionManager::UnrefTimestampHolder(TTransactionId transactionId)
{
    Impl_->UnrefTimestampHolder(transactionId);
}

DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionStarted, *Impl_);
DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionCommitted, *Impl_);
DELEGATE_SIGNAL(TTransactionManager, void(TTransaction*), TransactionAborted, *Impl_);
DELEGATE_BYREF_RO_PROPERTY(TTransactionManager, THashSet<TTransaction*>, TopmostTransactions, *Impl_);
DELEGATE_ENTITY_MAP_ACCESSORS(TTransactionManager, Transaction, TTransaction, *Impl_)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTransactionServer
