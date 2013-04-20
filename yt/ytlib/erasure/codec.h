#pragma once

#include "public.h"

#include <ytlib/misc/ref.h>
#include <ytlib/misc/nullable.h>

namespace NYT {
namespace NErasure {

////////////////////////////////////////////////////////////////////////////////

//! Describes a generic way to generate parity blocks from data blocks and
//! to recover (repair) missing blocks.
/*!
 *  Given N data blocks (numbered from 0 to N - 1) one can call #Encode to generate
 *  another M parity blocks (numbered from N to N + M - 1).
 *
 *  If some of the resulting N + M blocks ever become missing one can attempt to
 *  repair the missing blocks by calling #Decode.
 *
 *  Here N and M are fixed (codec-specific) parameters.
 *  Call #GetDataBlockCount and #GetParityBlockCount to figure out the
 *  the values for N and M, respectively.
 *
 */
struct ICodec
{
    //! Computes a sequence of parity blocks for given data blocks.
    /*!
     *  The size of #blocks must be equal to #GetDataBlockCount.
     *  The size of the returned array is equal to #GetParityBlockCount.
     */
    virtual std::vector<TSharedRef> Encode(const std::vector<TSharedRef>& blocks) = 0;

    //! Decodes (repairs) missing blocks.
    /*!
     *  #erasedIndices must contain the set of erased blocks indices.
     *  #blocks must contain known blocks (in the order specified by #GetRepairIndices).
     *  \returns The repaired blocks.
     */
    virtual std::vector<TSharedRef> Decode(
        const std::vector<TSharedRef>& blocks,
        const TBlockIndexList& erasedIndices) = 0;

    //! Given a set of missing block indices, returns |true| if missing blocks can be repaired.
    //! Due to performance reasons the elements of #erasedIndices must unique and sorted.
    virtual bool CanRepair(const TBlockIndexList& erasedIndices) = 0;

    //! Rapid version that works with set instead of list.
    virtual bool CanRepair(const TBlockIndexSet& erasedIndices) = 0;

    //! Given a set of missing block indices, checks if missing blocks can be repaired.
    /*!
     *  \returns
     *  If repair is not possible, returns |Null|.
     *  Otherwise returns the indices of blocks (both data and parity) to be passed to #Decode
     *  (in this very order). Not all known blocks may be needed for repair.
     */
    virtual TNullable<TBlockIndexList> GetRepairIndices(const TBlockIndexList& erasedIndices) = 0;

    //! Returns the number of data blocks this codec can handle.
    virtual int GetDataBlockCount() = 0;

    //! Returns the number of parity blocks this codec can handle.
    virtual int GetParityBlockCount() = 0;

    //! Every block passed to this codec must have size divisible by the result of #GetWordSize.
    virtual int GetWordSize() = 0;

    // Extension methods

    //! Returns the sum of #GetDataBlockCount and #GetParityBlockCount.
    int GetTotalBlockCount();
};

//! Returns the codec instance by its id.
/*!
 *  Codec instances are stateless and thread-safe.
 */
ICodec* GetCodec(ECodec id);

////////////////////////////////////////////////////////////////////////////////

} // namespace NErasure
} // namespace NYT


