#pragma once

#include "common.h"

#include "../misc/ref.h"

namespace NYT {
namespace NMetaState {

////////////////////////////////////////////////////////////////////////////////

//! This class synchronously performs all operations on the changelog.
/*!
 * TChangeLog also tries to ensure correctness of all operations and
 * handle all unforeseen exceptional situations. More detailed explanation
 * of verifications and guarantees can be found in the member documentation.
 */
class TChangeLog
    : public TRefCountedBase
{
public:
    typedef TIntrusivePtr<TChangeLog> TPtr;

    //! Basic constructor.
    TChangeLog(
        Stroka fileName,
        i32 id,
        i32 indexBlockSize = 1024 * 1024);

    void Open();
    void Create(i32 prevRecordCount);
    void Finalize();

    void Append(i32 recordId, TSharedRef recordData);
    void Flush();
    void Read(i32 firstRecordId, i32 recordCount, yvector<TSharedRef>* result);
    void Truncate(i32 atRecordId);

    i32 GetId() const;
    i32 GetPrevRecordCount() const;
    i32 GetRecordCount() const;
    bool IsFinalized() const;

private:
    class TImpl;

    THolder<TImpl> Impl;

};

////////////////////////////////////////////////////////////////////////////////

} // namespace NMetaState
} // namespace NYT
