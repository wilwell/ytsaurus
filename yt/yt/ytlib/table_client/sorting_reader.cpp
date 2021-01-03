#include "sorting_reader.h"
#include "timing_reader.h"

#include <yt/ytlib/chunk_client/dispatcher.h>

#include <yt/client/chunk_client/proto/data_statistics.pb.h>

#include <yt/client/table_client/name_table.h>
#include <yt/client/table_client/schemaless_row_reorderer.h>
#include <yt/client/table_client/unversioned_row_batch.h>
#include <yt/client/table_client/row_buffer.h>

#include <yt/core/concurrency/scheduler.h>

#include <yt/core/misc/error.h>

namespace NYT::NTableClient {

using namespace NChunkClient;
using namespace NChunkClient::NProto;
using namespace NConcurrency;

using NChunkClient::TDataSliceDescriptor;

////////////////////////////////////////////////////////////////////////////////

struct TSchemalessSortingReaderTag
{ };

class TSortingReader
    : public ISchemalessMultiChunkReader
    , public TTimingReaderBase
{
public:
    TSortingReader(
        ISchemalessMultiChunkReaderPtr underlyingReader,
        TNameTablePtr nameTable,
        TKeyColumns keyColumns,
        TComparator comparator)
        : UnderlyingReader_(std::move(underlyingReader))
        , KeyColumns_(std::move(keyColumns))
        , Comparator_(std::move(comparator))
        , RowBuffer_(New<TRowBuffer>(TSchemalessSortingReaderTag()))
        , RowReorderer_(std::move(nameTable), RowBuffer_, /* deepCapture */ true, KeyColumns_)
    {
        YT_VERIFY(KeyColumns_.size() == Comparator_.GetLength());

        SetReadyEvent(BIND(&TSortingReader::DoOpen, MakeWeak(this))
            .AsyncVia(TDispatcher::Get()->GetReaderInvoker())
            .Run());
    }

    virtual IUnversionedRowBatchPtr Read(const TRowBatchReadOptions& options) override
    {
        if (!ReadyEvent().IsSet() || !ReadyEvent().Get().IsOK()) {
            return CreateEmptyUnversionedRowBatch();
        }

        i64 startRowCount = ReadRowCount_;
        i64 endRowCount = startRowCount;
        i64 dataWeight = 0;
        while (endRowCount < Rows_.size() && endRowCount - startRowCount < options.MaxRowsPerRead && dataWeight < options.MaxDataWeightPerRead) {
            dataWeight += GetDataWeight(Rows_[endRowCount++]);
        }

        if (endRowCount == startRowCount) {
            return nullptr;
        }

        ReadDataWeight_ += dataWeight;
        ReadRowCount_ = endRowCount;

        return CreateBatchFromUnversionedRows(TSharedRange<TUnversionedRow>(
            TRange<TUnversionedRow>(Rows_.data() + startRowCount, Rows_.data() + endRowCount),
            MakeStrong(this)));
    }

    virtual bool IsFetchingCompleted() const override
    {
        YT_VERIFY(UnderlyingReader_);
        return UnderlyingReader_->IsFetchingCompleted();
    }

    virtual TDataStatistics GetDataStatistics() const override
    {
        YT_VERIFY(UnderlyingReader_);
        auto dataStatistics = UnderlyingReader_->GetDataStatistics();
        dataStatistics.set_row_count(ReadRowCount_);
        dataStatistics.set_data_weight(ReadDataWeight_);
        return dataStatistics;
    }

    TCodecStatistics GetDecompressionStatistics() const override
    {
        YT_VERIFY(UnderlyingReader_);
        return UnderlyingReader_->GetDecompressionStatistics();
    }

    virtual std::vector<TChunkId> GetFailedChunkIds() const override
    {
        YT_VERIFY(UnderlyingReader_);
        return UnderlyingReader_->GetFailedChunkIds();
    }

    virtual TInterruptDescriptor GetInterruptDescriptor(
        TRange<TUnversionedRow> /* unreadRows */) const override
    {
        YT_ABORT();
    }

    virtual void Interrupt() override
    {
        YT_ABORT();
    }

    virtual void SkipCurrentReader() override
    {
        YT_ABORT();
    }

    virtual const TNameTablePtr& GetNameTable() const override
    {
        return UnderlyingReader_->GetNameTable();
    }

    virtual i64 GetTotalRowCount() const override
    {
        return Rows_.size();
    }

    virtual i64 GetSessionRowIndex() const override
    {
        return ReadRowCount_;
    }

    virtual i64 GetTableRowIndex() const override
    {
        return 0;
    }

    virtual const TDataSliceDescriptor& GetCurrentReaderDescriptor() const
    {
        YT_ABORT();
    }

private:
    const ISchemalessMultiChunkReaderPtr UnderlyingReader_;
    const TKeyColumns KeyColumns_;
    const TComparator Comparator_;

    const TRowBufferPtr RowBuffer_;
    TSchemalessRowReorderer RowReorderer_;

    std::vector<TUnversionedRow> Rows_;
    i64 ReadRowCount_ = 0;
    i64 ReadDataWeight_ = 0;

    void DoOpen()
    {
        while (auto batch = UnderlyingReader_->Read()) {
            if (batch->IsEmpty()) {
                WaitFor(UnderlyingReader_->GetReadyEvent())
                    .ThrowOnError();
                continue;
            }

            for (auto row : batch->MaterializeRows()) {
                Rows_.push_back(RowReorderer_.ReorderRow(row));
            }
        }

        std::sort(
            Rows_.begin(),
            Rows_.end(),
            [&] (auto lhsRow, auto rhsRow) {
                // Value types validation is disabled for performance reasons.
                auto lhsKey = TKey::FromRowUnchecked(lhsRow, KeyColumns_.size());
                auto rhsKey = TKey::FromRowUnchecked(rhsRow, KeyColumns_.size());
                return Comparator_.CompareKeys(lhsKey, rhsKey) < 0;
            });
    }
};

////////////////////////////////////////////////////////////////////////////////

ISchemalessMultiChunkReaderPtr CreateSortingReader(
    ISchemalessMultiChunkReaderPtr underlyingReader,
    TNameTablePtr nameTable,
    TKeyColumns keyColumns,
    TComparator comparator)
{
    return New<TSortingReader>(
        std::move(underlyingReader),
        std::move(nameTable),
        std::move(keyColumns),
        std::move(comparator));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
