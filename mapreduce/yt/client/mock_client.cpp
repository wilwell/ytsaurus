#include "mock_client.h"

#include <util/generic/guid.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace {

    TNodeId GetDefaultGuid() {
        TGUID guid;
        CreateGuid(&guid);
        return guid;
    }

    class TMockTransaction
        : public ITransaction
        , public TMockClient
    {
    public:
        TMockTransaction()
            : TransactionId_(GetDefaultGuid())
        {
        }

        const TTransactionId& GetId() const override
        {
            return TransactionId_;
        }

        TLockId Lock(
            const TYPath&,
            ELockMode,
            const TLockOptions&) override
        {
            return GetDefaultGuid();
        }
        void Commit() override {}
        void Abort() override {}

    private:
        TTransactionId TransactionId_;
    };

    class TMockFileReader
        : public IFileReader
    {
    protected:
        size_t DoRead(void*, size_t) override {
            return 0;
        }
    };

    class TMockFileWriter
        : public IFileWriter
    {
    public:
        ~TMockFileWriter() override {}

    protected:
        void DoWrite(const void*, size_t) override {}
        void DoFinish() override {}
    };

    class TMockNodeTableReader
        : public INodeReaderImpl
    {
    public:
        ~TMockNodeTableReader() override {}

        const TNode& GetRow() const override {
            return Default;
        }

        bool IsValid() const override {
            return false;
        }

        void Next() override {}

        ui32 GetTableIndex() const override {
            return 0;
        }

        ui64 GetRowIndex() const override {
            return 0;
        }

        void NextKey() override {}
    private:
        TNode Default;
    };


    class TMockNodeTableWriter
        : public INodeWriterImpl
    {
    public:
        ~TMockNodeTableWriter() override {}

        void AddRow(const TNode&, size_t) override {}
        void Finish() override {}
    };

    class TMockProtoTableReader
        : public IProtoReaderImpl
    {
    public:
        ~TMockProtoTableReader() override {}

        void ReadRow(Message*) override {}
        void SkipRow() override {}
        bool IsValid() const override {
            return false;
        }
        void Next() override {}
        ui32 GetTableIndex() const override {
            return 0;
        }
        ui64 GetRowIndex() const override {
            return 0;
        }
        void NextKey() override {}
    };

    class TMockProtoTableWriter
        : public IProtoWriterImpl
    {
    public:
        ~TMockProtoTableWriter() override {}

        void AddRow(const Message&, size_t) override {}
        void Finish() override {}
    };

    class TMockYaMRTableReader
        : public IYaMRReaderImpl
    {
    public:
        ~TMockYaMRTableReader() override {}

        const TYaMRRow& GetRow() const override {
            return Default;
        }
        bool IsValid() const override {
            return false;
        }
        void Next() override {}
        ui32 GetTableIndex() const override {
            return 0;
        }
        ui64 GetRowIndex() const override {
            return 0;
        }
        void NextKey() override {}
    private:
        TYaMRRow Default;
    };


    class TMockYaMRTableWriter
        : public IYaMRWriterImpl
    {
    public:
        ~TMockYaMRTableWriter() override {}

        void AddRow(const TYaMRRow&, size_t) override {}
        void Finish() override {}
    };

} // namespace




TNodeId TMockClient::Create(const TYPath&, ENodeType, const TCreateOptions&) {
    return GetDefaultGuid();
}

void TMockClient::Remove(const TYPath&, const TRemoveOptions&) {
}

bool TMockClient::Exists(const TYPath&) {
    return true;
}

TNode TMockClient::Get(const TYPath&, const TGetOptions&) {
    return TNode();
}

void TMockClient::Set(const TYPath&, const TNode&) {
}

TNode::TList TMockClient::List(const TYPath&, const TListOptions&) {
    return TNode::TList();
}

TNodeId TMockClient::Copy(const TYPath&, const TYPath&, const TCopyOptions&) {
    return GetDefaultGuid();
}
TNodeId TMockClient::Move(const TYPath&, const TYPath&, const TMoveOptions&) {
    return GetDefaultGuid();
}
TNodeId TMockClient::Link(const TYPath&, const TYPath&, const TLinkOptions&) {
    return GetDefaultGuid();
}
void TMockClient::Concatenate(const yvector<TYPath>&, const TYPath&, const TConcatenateOptions&) {
}

IFileReaderPtr TMockClient::CreateFileReader(const TRichYPath&, const TFileReaderOptions&) {
    return new TMockFileReader();
}

IFileWriterPtr TMockClient::CreateFileWriter(const TRichYPath&, const TFileWriterOptions&) {
    return new TMockFileWriter();
}

TIntrusivePtr<INodeReaderImpl> TMockClient::CreateNodeReader(const TRichYPath&, const TTableReaderOptions&) {
    return new TMockNodeTableReader();
}

TIntrusivePtr<IYaMRReaderImpl> TMockClient::CreateYaMRReader(const TRichYPath&, const TTableReaderOptions&) {
    return new TMockYaMRTableReader();
}

TIntrusivePtr<IProtoReaderImpl> TMockClient::CreateProtoReader(const TRichYPath&, const TTableReaderOptions&) {
    return new TMockProtoTableReader();
}

TIntrusivePtr<INodeWriterImpl> TMockClient::CreateNodeWriter(const TRichYPath&, const TTableWriterOptions&) {
    return new TMockNodeTableWriter();
}

TIntrusivePtr<IYaMRWriterImpl> TMockClient::CreateYaMRWriter(const TRichYPath&, const TTableWriterOptions&) {
    return new TMockYaMRTableWriter();
}

TIntrusivePtr<IProtoWriterImpl> TMockClient::CreateProtoWriter(const TRichYPath&, const TTableWriterOptions&) {
    return new TMockProtoTableWriter();
}

TOperationId TMockClient::Sort(const TSortOperationSpec&, const TOperationOptions&) {
    return GetDefaultGuid();
}

TOperationId TMockClient::Merge(const TMergeOperationSpec&, const TOperationOptions&) {
    return GetDefaultGuid();
}

TOperationId TMockClient::Erase(const TEraseOperationSpec&, const TOperationOptions&) {
    return GetDefaultGuid();
}

void TMockClient::AbortOperation(const TOperationId&) {
}

void TMockClient::WaitForOperation(const TOperationId&) {
}

EOperationStatus TMockClient::CheckOperation(const TOperationId&) {
    return OS_COMPLETED;
}


TOperationId TMockClient::DoMap(const TMapOperationSpec&, IJob*, const TOperationOptions&) {
    return GetDefaultGuid();
}

TOperationId TMockClient::DoReduce(const TReduceOperationSpec&, IJob*, const TOperationOptions&) {
    return GetDefaultGuid();
}

TOperationId TMockClient::DoJoinReduce(const TJoinReduceOperationSpec&, IJob*, const TOperationOptions&) {
    return GetDefaultGuid();
}

TOperationId TMockClient::DoMapReduce(const TMapReduceOperationSpec&, IJob*, IJob*, IJob*, const TMultiFormatDesc&, const TMultiFormatDesc&, const TMultiFormatDesc&, const TMultiFormatDesc&, const TOperationOptions&) {
    return GetDefaultGuid();
}

ITransactionPtr TMockClient::StartTransaction(const TStartTransactionOptions&) {
    return new TMockTransaction();
}

ITransactionPtr TMockClient::AttachTransaction(const TTransactionId&) {
    return new TMockTransaction();
}

void TMockClient::InsertRows(const TYPath&, const TNode::TList&) {
}

void TMockClient::DeleteRows(const TYPath&, const TNode::TList&) {
}

TNode::TList TMockClient::LookupRows(const TYPath&, const TNode::TList&, const TLookupRowsOptions&) {
    return TNode::TList();
}

TNode::TList TMockClient::SelectRows(const Stroka&, const TSelectRowsOptions&) {
    return TNode::TList();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
