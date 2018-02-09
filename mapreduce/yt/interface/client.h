#pragma once

#include "fwd.h"

#include "client_method_options.h"
#include "batch_request.h"
#include "cypress.h"
#include "init.h"
#include "io.h"
#include "node.h"
#include "operation.h"

#include <library/threading/future/future.h>

#include <util/datetime/base.h>
#include <util/generic/maybe.h>
#include <util/system/compiler.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

class ILock
    : public TThrRefBase
{
public:
    virtual ~ILock() = default;

    virtual const TLockId& GetId() const = 0;

    // Returns future that will be set once lock is in "acquired" state.
    //
    // Note that future might contain exception if some error occurred
    // e.g. lock transaction was aborted.
    virtual const NThreading::TFuture<void>& GetAcquiredFuture() const = 0;

    // Convenient wrapper that waits until lock is in "acquired" state.
    // Throws exception if timeout exceeded or some error occurred
    // e.g. lock transaction was aborted.
    void Wait(TDuration timeout = TDuration::Max());
};

class IClientBase
    : public TThrRefBase
    , public ICypressClient
    , public IIOClient
    , public IOperationClient
{
public:
    virtual Y_WARN_UNUSED_RESULT ITransactionPtr StartTransaction(
        const TStartTransactionOptions& options = TStartTransactionOptions()) = 0;

    //
    // Change properties of table:
    //   - switch table between dynamic/static mode
    //   - or change table schema
    virtual void AlterTable(
        const TYPath& path,
        const TAlterTableOptions& options = TAlterTableOptions()) = 0;

    //
    // Create batch request object that allows to execute several light requests in parallel.
    // https://wiki.yandex-team.ru/yt/userdoc/api/#executebatch18.4
    virtual TBatchRequestPtr CreateBatchRequest() = 0;
};

class ITransaction
    : virtual public IClientBase
{
public:
    virtual const TTransactionId& GetId() const = 0;

    //
    // Try to lock given path. Lock will be held until transaction is commited/aborted.
    // Lock modes:
    //   LM_EXCLUSIVE: if exclusive lock is taken no other transaction can take exclusive or shared lock.
    //   LM_SHARED: if shared lock is taken other transactions can take shared lock but not exclusive.
    //
    //   LM_SNAPSHOT: snapshot lock always succeeds, when snapshot lock is taken current transaction snapshots object.
    //   It will not see changes that occured to it in other transactions.
    //
    // Exclusive/shared lock can be waitable or not.
    // If nonwaitable lock cannot be taken exception is thrown.
    // If waitable lock cannot be taken it is created in pending state and client can wait until it actually taken.
    // Check TLockOptions::Waitable and ILock::GetAcquiredFuture for more details.
    virtual ILockPtr Lock(
        const TYPath& path,
        ELockMode mode,
        const TLockOptions& options = TLockOptions()) = 0;

    //
    // Commit transaction. All changes that are made by transactions become visible globaly or to parent transaction.
    virtual void Commit() = 0;

    //
    // Abort transaction. All changes that are made by current transaction are lost.
    virtual void Abort() = 0;
};

class IClient
    : virtual public IClientBase
{
public:
    virtual Y_WARN_UNUSED_RESULT ITransactionPtr AttachTransaction(
        const TTransactionId& transactionId) = 0;

    virtual void MountTable(
        const TYPath& path,
        const TMountTableOptions& options = TMountTableOptions()) = 0;

    virtual void UnmountTable(
        const TYPath& path,
        const TUnmountTableOptions& options = TUnmountTableOptions()) = 0;

    virtual void RemountTable(
        const TYPath& path,
        const TRemountTableOptions& options = TRemountTableOptions()) = 0;

    // Switch dynamic table from `mounted' into `frozen' state.
    // When table is in frozen state all its data is flushed to disk and writes are disabled.
    //
    // NOTE: this function launches the process of switching, but doesn't wait until switching is accomplished.
    // Waiting has to be performed by user.
    virtual void FreezeTable(
        const TYPath& path,
        const TFreezeTableOptions& options = TFreezeTableOptions()) = 0;

    // Switch dynamic table from `frozen' into `mounted' state.
    //
    // NOTE: this function launches the process of switching, but doesn't wait until switching is accomplished.
    // Waiting has to be performed by user.
    virtual void UnfreezeTable(
        const TYPath& path,
        const TUnfreezeTableOptions& options = TUnfreezeTableOptions()) = 0;

    virtual void ReshardTable(
        const TYPath& path,
        const TVector<TKey>& pivotKeys,
        const TReshardTableOptions& options = TReshardTableOptions()) = 0;

    virtual void ReshardTable(
        const TYPath& path,
        i64 tabletCount,
        const TReshardTableOptions& options = TReshardTableOptions()) = 0;

    virtual void InsertRows(
        const TYPath& path,
        const TNode::TListType& rows,
        const TInsertRowsOptions& options = TInsertRowsOptions()) = 0;

    virtual void DeleteRows(
        const TYPath& path,
        const TNode::TListType& keys,
        const TDeleteRowsOptions& options = TDeleteRowsOptions()) = 0;

    virtual void TrimRows(
        const TYPath& path,
        i64 tabletIndex,
        i64 rowCount,
        const TTrimRowsOptions& options = TTrimRowsOptions()) = 0;

    virtual TNode::TListType LookupRows(
        const TYPath& path,
        const TNode::TListType& keys,
        const TLookupRowsOptions& options = TLookupRowsOptions()) = 0;

    virtual TNode::TListType SelectRows(
        const TString& query,
        const TSelectRowsOptions& options = TSelectRowsOptions()) = 0;

    // Is not supported since YT 19.2 version
    virtual void EnableTableReplica(const TReplicaId& replicaid) = 0;

    // Is not supported since YT 19.2 version
    virtual void DisableTableReplica(const TReplicaId& replicaid) = 0;

    // Change properties of table replica.
    // Allows to enable/disable replica and/or change its mode.
    virtual void AlterTableReplica(
        const TReplicaId& replicaId,
        const TAlterTableReplicaOptions& alterTableReplicaOptions) = 0;

    virtual ui64 GenerateTimestamp() = 0;
};

IClientPtr CreateClient(
    const TString& serverName,
    const TCreateClientOptions& options = TCreateClientOptions());

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
