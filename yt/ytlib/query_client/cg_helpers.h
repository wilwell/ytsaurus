#pragma once

#include "cg_ir_builder.h"
#include "cg_types.h"

#include <yt/core/codegen/module.h>

namespace NYT {
namespace NQueryClient {

// Import extensively used LLVM types.
using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantFP;
using llvm::ConstantInt;
using llvm::ConstantPointerNull;
using llvm::Function;
using llvm::FunctionType;
using llvm::Instruction;
using llvm::PHINode;
using llvm::PointerType;
using llvm::Twine;
using llvm::Type;
using llvm::TypeBuilder;
using llvm::Value;

using NCodegen::TCGModulePtr;

////////////////////////////////////////////////////////////////////////////////

class TCGFunctionContext
{
protected:
    std::vector<Value*> OpaqueValues_;
    Value* ExecutionContextPtr_;
    Value* Buffer_;

public:
    const TCGModulePtr Module;

    TCGFunctionContext(
        std::vector<Value*> opaqueValues,
        Value* executionContextPtr,
        const TCGModulePtr module)
        : OpaqueValues_(std::move(opaqueValues))
        , ExecutionContextPtr_(executionContextPtr)
        , Module(std::move(module))
    { }

};

TCGFunctionContext MakeCGFunctionContext(
    TCGIRBuilder& builder,
    Value* opaqueValues,
    size_t opaqueValuesCount,
    Value* executionContextPtr,
    const TCGModulePtr module);

class TCGContext
    : public TCGIRBuilder
    , public TCGFunctionContext
{
public:
    TCGContext(
        const TCGIRBuilder& builder,
        const TCGFunctionContext& parent)
        : TCGIRBuilder(builder)
        , TCGFunctionContext(parent)
    { }

    Value* GetOpaqueValue(size_t index)
    {
        return ViaClosure(OpaqueValues_[index], "opaqueValues." + Twine(index));
    }

    Value* GetExecutionContextPtr()
    {
        return ViaClosure(ExecutionContextPtr_, "executionContextPtr");
    }

    Value* GetBuffer()
    {
        return ViaClosure(Buffer_, "bufferPtr");
    }

    Value* GetBufferValue()
    {
        return Buffer_;
    }

    void SetBuffer(Value* buffer)
    {
        Buffer_ = buffer;
    }

};

Value* CodegenValuesPtrFromRow(TCGIRBuilder&, Value*);

typedef TypeBuilder<TValue, false> TTypeBuilder;
typedef TypeBuilder<TValueData, false> TDataTypeBuilder;

class TCGValue
{
private:
    Value* IsNull_;
    Value* Length_;
    Value* Data_;
    EValueType StaticType_;
    std::string Name_;

    TCGValue(Value* isNull, Value* length, Value* data, EValueType staticType, Twine name)
        : IsNull_(isNull)
        , Length_(length)
        , Data_(data)
        , StaticType_(staticType)
        , Name_(name.str())
    {
        YCHECK(
            StaticType_ == EValueType::Int64 ||
            StaticType_ == EValueType::Uint64 ||
            StaticType_ == EValueType::Double ||
            StaticType_ == EValueType::Boolean ||
            StaticType_ == EValueType::String ||
            StaticType_ == EValueType::Any);
    }

public:
    TCGValue(const TCGValue& other) = default;

    TCGValue(TCGValue&& other)
        : IsNull_(other.IsNull_)
        , Length_(other.Length_)
        , Data_(other.Data_)
        , StaticType_(other.StaticType_)
        , Name_(std::move(other.Name_))
    {
        other.Reset();
    }

    TCGValue& operator=(TCGValue&& other)
    {
        IsNull_ = other.IsNull_;
        Length_ = other.Length_;
        Data_ = other.Data_;
        StaticType_ = other.StaticType_;

        other.Reset();

        return *this;
    }

    TCGValue&& Steal()
    {
        return std::move(*this);
    }

    void Reset()
    {
        IsNull_ = nullptr;
        Length_ = nullptr;
        Data_ = nullptr;
        StaticType_ = EValueType::TheBottom;
    }

    EValueType GetStaticType() const
    {
        return StaticType_;
    }

    static TCGValue CreateFromValue(
        TCGIRBuilder& builder,
        Value* isNull,
        Value* length,
        Value* data,
        EValueType staticType,
        Twine name = Twine())
    {
        if (isNull) {
            YCHECK(isNull->getType() == builder.getInt1Ty());
        }
        if (length) {
            YCHECK(length->getType() == TTypeBuilder::TLength::get(builder.getContext()));
        }
        if (data) {
            YCHECK(data->getType() == TDataTypeBuilder::get(builder.getContext(), staticType));
        }
        return TCGValue(isNull, length, data, staticType, name);
    }

    static TCGValue CreateFromRow(
        TCGIRBuilder& builder,
        Value* row,
        int index,
        EValueType staticType,
        Twine name = Twine())
    {
        auto valuePtr = builder.CreateConstInBoundsGEP1_32(
            nullptr,
            CodegenValuesPtrFromRow(builder, row),
            index,
            name + ".valuePtr");

        return CreateFromLlvmValue(
            builder,
            valuePtr,
            staticType,
            name);
    }

    static TCGValue CreateFromLlvmValue(
        TCGIRBuilder& builder,
        Value* valuePtr,
        EValueType staticType,
        Twine name = Twine())
    {
        auto type = builder.CreateLoad(
            builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Type, name + ".typePtr"),
            name + ".type");
        auto length = builder.CreateLoad(
            builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Length, name + ".lengthPtr"),
            name + ".length");
        auto data = builder.CreateLoad(
            builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Data, name + ".dataPtr"),
            name + ".data");

        Type* targetType = TDataTypeBuilder::get(builder.getContext(), staticType);

        Value* castedData = nullptr;

        if (targetType->isPointerTy()) {
            castedData = builder.CreateIntToPtr(data,
                targetType,
                name + ".data");
        } else if (targetType->isFloatingPointTy()) {
            castedData = builder.CreateBitCast(data,
                targetType,
                name + ".data");
        } else {
            castedData = builder.CreateIntCast(data,
                targetType,
                false,
                name + ".data");
        }

        auto isNull = builder.CreateICmpEQ(
            type,
            ConstantInt::get(type->getType(), static_cast<int>(EValueType::Null)),
            name + ".isNull");

        return CreateFromValue(builder, isNull, length, castedData, staticType, name);
    }

    static TCGValue CreateNull(
        TCGIRBuilder& builder,
        EValueType staticType,
        Twine name = Twine())
    {
        return CreateFromValue(
            builder,
            builder.getInt1(true),
            llvm::UndefValue::get(TTypeBuilder::TLength::get(builder.getContext())),
            llvm::UndefValue::get(TDataTypeBuilder::get(builder.getContext(), staticType)),
            staticType,
            name);
    }

    void StoreToRow(TCGIRBuilder& builder, Value* row, int index, ui16 id)
    {
        auto name = row->getName();

        auto valuePtr = builder.CreateConstInBoundsGEP1_32(
            nullptr,
            CodegenValuesPtrFromRow(builder, row),
            index,
            Twine(name).concat(".at.") + Twine(index));

        StoreToValue(builder, valuePtr, id);
    }

    void StoreToValue(TCGIRBuilder& builder, Value* valuePtr, ui16 id, Twine nameTwine = "")
    {
        builder.CreateStore(
            builder.getInt16(id),
            builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Id, nameTwine + ".idPtr"));

        StoreToValue(builder, valuePtr, nameTwine);
    }

    void StoreToValue(TCGIRBuilder& builder, Value* valuePtr, Twine nameTwine = "")
    {
        if (IsNull_) {
            builder.CreateStore(
                GetType(builder),
                builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Type, nameTwine + ".typePtr"));
        }
        if (Length_) {
            builder.CreateStore(
                Length_,
                builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Length, nameTwine + ".lengthPtr"));
        }
        if (Data_) {
            Value* data = nullptr;
            auto targetType = TDataTypeBuilder::get(builder.getContext());

            if (Data_->getType()->isPointerTy()) {
                data = builder.CreatePtrToInt(Data_, targetType);
            } else if (Data_->getType()->isFloatingPointTy()) {
                data = builder.CreateBitCast(Data_, targetType);
            } else {
                data = builder.CreateIntCast(Data_, targetType, false);
            }

            builder.CreateStore(
                data,
                builder.CreateStructGEP(nullptr, valuePtr, TTypeBuilder::Data, nameTwine + ".dataPtr"));
        }
    }

    Value* IsNull()
    {
        YCHECK(IsNull_);
        return IsNull_;
    }

    Value* GetType(TCGIRBuilder& builder)
    {
        const auto& type = TypeBuilder<NTableClient::TUnversionedValue, false>::TType::get(builder.getContext());
        return builder.CreateSelect(
            IsNull(),
            ConstantInt::get(type, static_cast<int>(EValueType::Null)),
            ConstantInt::get(type, static_cast<int>(StaticType_)));
    }

    Value* GetLength()
    {
        return Length_;
    }

    Value* GetData()
    {
        return Data_;
    }

    TCGValue Cast(TCGIRBuilder& builder, EValueType dest, bool bitcast = false)
    {
        if (dest == StaticType_) {
            return *this;
        }

        auto value = GetData();

        Value* result;
        if (dest == EValueType::Int64) {
            auto destType = TDataTypeBuilder::TUint64::get(builder.getContext());
            if (bitcast) {
                result = builder.CreateBitCast(value, destType);
            } else if (StaticType_ == EValueType::Uint64 || StaticType_ == EValueType::Boolean) {
                result = builder.CreateIntCast(value, destType, false);
            } else if (StaticType_ == EValueType::Double) {
                result = builder.CreateFPToSI(value, destType);
            } else {
                YUNREACHABLE();
            }
        } else if (dest == EValueType::Uint64) {
            auto destType = TDataTypeBuilder::TUint64::get(builder.getContext());
            if (bitcast) {
                result = builder.CreateBitCast(value, destType);
            } else if (StaticType_ == EValueType::Int64 || StaticType_ == EValueType::Boolean) {
                result = builder.CreateIntCast(value, destType, true);
            } else if (StaticType_ == EValueType::Double) {
                result = builder.CreateFPToUI(value, destType);
            } else {
                YUNREACHABLE();
            }
        } else if (dest == EValueType::Double) {
            auto destType = TDataTypeBuilder::TDouble::get(builder.getContext());
            if (bitcast) {
                result = builder.CreateBitCast(value, destType);
            } else if (StaticType_ == EValueType::Uint64) {
                result = builder.CreateUIToFP(value, destType);
            } else if (StaticType_ == EValueType::Int64) {
                result = builder.CreateSIToFP(value, destType);
            } else {
                YUNREACHABLE();
            }
        } else {
            YUNREACHABLE();
        }

        return CreateFromValue(builder, IsNull(), GetLength(), result, dest);
    }
};

////////////////////////////////////////////////////////////////////////////////
TCGValue MakePhi(
    TCGIRBuilder& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    TCGValue thenValue,
    TCGValue elseValue,
    Twine name = Twine());

Value* MakePhi(
    TCGIRBuilder& builder,
    BasicBlock* thenBB,
    BasicBlock* elseBB,
    Value* thenValue,
    Value* elseValue,
    Twine name = Twine());

template <class TBuilder, class TResult>
TResult CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<TResult(TBuilder& builder)>& thenCodegen,
    const std::function<TResult(TBuilder& builder)>& elseCodegen,
    Twine name = Twine())
{
    auto* thenBB = builder.CreateBBHere("then");
    auto* elseBB = builder.CreateBBHere("else");
    auto* endBB = builder.CreateBBHere("end");

    builder.CreateCondBr(condition, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);
    auto thenValue = thenCodegen(builder);
    builder.CreateBr(endBB);
    thenBB = builder.GetInsertBlock();

    builder.SetInsertPoint(elseBB);
    auto elseValue = elseCodegen(builder);
    builder.CreateBr(endBB);
    elseBB = builder.GetInsertBlock();

    builder.SetInsertPoint(endBB);

    return MakePhi(builder, thenBB, elseBB, thenValue, elseValue, name);
}

template <class TBuilder>
void CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<void(TBuilder& builder)>& thenCodegen,
    const std::function<void(TBuilder& builder)>& elseCodegen)
{
    auto* thenBB = builder.CreateBBHere("then");
    auto* elseBB = builder.CreateBBHere("else");
    auto* endBB = builder.CreateBBHere("end");

    builder.CreateCondBr(condition, thenBB, elseBB);

    builder.SetInsertPoint(thenBB);
    thenCodegen(builder);
    builder.CreateBr(endBB);
    thenBB = builder.GetInsertBlock();

    builder.SetInsertPoint(elseBB);
    elseCodegen(builder);
    builder.CreateBr(endBB);
    elseBB = builder.GetInsertBlock();

    builder.SetInsertPoint(endBB);
}

template <class TBuilder>
void CodegenIf(
    TBuilder& builder,
    Value* condition,
    const std::function<void(TBuilder& builder)>& thenCodegen)
{
    CodegenIf<TBuilder>(
        builder,
        condition,
        thenCodegen,
        [&] (TBuilder& builder) {
        });
}

////////////////////////////////////////////////////////////////////////////////

template <class TSequence>
struct TApplyCallback;

template <unsigned... Indexes>
struct TApplyCallback<NMpl::TSequence<Indexes...>>
{
    template <class TBody, class TBuilder>
    static void Do(TBody&& body, TBuilder&& builder, Value* argsArray[sizeof...(Indexes)])
    {
        body(builder, argsArray[Indexes]...);
    }
};

////////////////////////////////////////////////////////////////////////////////

struct TLlvmClosure
{
    Value* ClosurePtr;
    llvm::Function* Function;
};

template <class TSignature>
struct TClosureFunctionDefiner;

template <class TResult, class... TArgs>
struct TClosureFunctionDefiner<TResult(TArgs...)>
{
    typedef typename NMpl::TGenerateSequence<sizeof...(TArgs)>::TType TIndexesPack;

    template <class TBody>
    static TLlvmClosure Do(llvm::Module* module, TCGContext& parentBuilder, TBody&& body, llvm::Twine name)
    {
        Function* function = Function::Create(
            TypeBuilder<TResult(void**, TArgs...), false>::get(module->getContext()),
            Function::ExternalLinkage,
            name,
            module);

        function->addFnAttr(llvm::Attribute::AttrKind::UWTable);

        auto args = function->arg_begin();
        Value* closurePtr = args++; closurePtr->setName("closure");

        Value* argsArray[sizeof...(TArgs)];
        size_t index = 0;
        while (args != function->arg_end()) {
            argsArray[index++] = args++;
        }
        YCHECK(index == sizeof...(TArgs));

        std::unordered_set<llvm::Value*> valuesInContext;
        TCGIRBuilder baseBuilder(function, &valuesInContext, &parentBuilder, closurePtr);
        TCGContext builder(baseBuilder, parentBuilder);

        TApplyCallback<TIndexesPack>::template Do(std::forward<TBody>(body), builder, argsArray);

        return TLlvmClosure{builder.GetClosure(), function};
    }
};

template <class TSignature, class TBody>
TLlvmClosure MakeClosure(TCGContext& builder, llvm::Twine name, TBody&& body)
{
    return TClosureFunctionDefiner<TSignature>::Do(
        builder.Module->GetModule(),
        builder,
        std::forward<TBody>(body),
        name);
}

template <class TSignature>
struct TFunctionDefiner;

template <class TResult, class... TArgs>
struct TFunctionDefiner<TResult(TArgs...)>
{
    typedef typename NMpl::TGenerateSequence<sizeof...(TArgs)>::TType TIndexesPack;

    template <class TBody>
    static Function* Do(llvm::Module* module, TBody&& body, llvm::Twine name)
    {
        Function* function =  Function::Create(
            TypeBuilder<TResult(TArgs...), false>::get(module->getContext()),
            Function::ExternalLinkage,
            name,
            module);

        function->addFnAttr(llvm::Attribute::AttrKind::UWTable);

        auto args = function->arg_begin();
        Value* argsArray[sizeof...(TArgs)];
        size_t index = 0;
        while (args != function->arg_end()) {
            argsArray[index++] = args++;
        }
        YCHECK(index == sizeof...(TArgs));

        std::unordered_set<llvm::Value*> valuesInContext;
        TCGIRBuilder builder(function, &valuesInContext);
        TApplyCallback<TIndexesPack>::template Do(std::forward<TBody>(body), builder, argsArray);

        return function;
    }
};

template <class TSignature, class TBody>
Function* MakeFunction(llvm::Module* module, llvm::Twine name, TBody&& body)
{
    auto function = TFunctionDefiner<TSignature>::Do(
        module,
        std::forward<TBody>(body),
        name);

    return function;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
