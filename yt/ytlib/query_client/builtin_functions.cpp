#include "builtin_functions.h"

#include "cg_fragment_compiler.h"
#include "plan_helpers.h"
#include "private.h"

namespace NYT {
namespace NQueryClient {

using namespace NVersionedTableClient;

////////////////////////////////////////////////////////////////////////////////

TTypedFunction::TTypedFunction(
    const Stroka& functionName,
    std::vector<TType> argumentTypes,
    TType repeatedArgumentType,
    TType resultType)
    : FunctionName_(functionName)
    , ArgumentTypes_(argumentTypes)
    , RepeatedArgumentType_(repeatedArgumentType)
    , ResultType_(resultType)
{ }

TTypedFunction::TTypedFunction(
    const Stroka& functionName,
    std::vector<TType> argumentTypes,
    TType resultType)
    : FunctionName_(functionName)
    , ArgumentTypes_(argumentTypes)
    , RepeatedArgumentType_(EValueType::Null)
    , ResultType_(resultType)
{ }

Stroka TTypedFunction::GetName() const
{
    return FunctionName_;
}

EValueType TTypedFunction::InferResultType(
    const std::vector<EValueType>& argumentTypes,
    const TStringBuf& source) const
{
    return TypingFunction(
        ArgumentTypes_,
        RepeatedArgumentType_,
        ResultType_,
        GetName(),
        argumentTypes,
        source);
}

Stroka TypeToString(TType tp, std::unordered_map<TTypeArgument, EValueType> genericAssignments)
{
    if (auto genericId = tp.TryAs<TTypeArgument>()) {
        return TypeToString(genericAssignments[*genericId], genericAssignments);
    } else if (auto unionType = tp.TryAs<TUnionType>()) {
        Stroka unionString = "one of { ";
        for (auto tp = (*unionType).begin(); tp != (*unionType).end(); tp++) {
            if (tp != (*unionType).begin()) {
                unionString += ", ";
            }
            unionString += TypeToString(*tp, genericAssignments);
        }
        return unionString + " }";
    } else {
        return ToString(tp.As<EValueType>());
    }
}

EValueType TTypedFunction::TypingFunction(
    const std::vector<TType>& expectedArgTypes,
    TType repeatedArgType,
    TType resultType,
    const Stroka& functionName,
    const std::vector<EValueType>& argTypes,
    const TStringBuf& source) const
{
    std::unordered_map<TTypeArgument, EValueType> genericAssignments;

    auto isSubtype = [&] (EValueType type1, TType type2) {
        YCHECK(!type2.TryAs<TTypeArgument>());
        if (auto* unionType = type2.TryAs<TUnionType>()) {
            auto exists = std::find(
                unionType->begin(),
                unionType->end(),
                type1);
            return exists != unionType->end();
        } else if (auto* concreteType = type2.TryAs<EValueType>()) {
            return type1 == *concreteType;
        }
        return false;
    };

    auto unify = [&] (TType type1, EValueType type2) {
        if (auto* genericId = type1.TryAs<TTypeArgument>()) {
            if (genericAssignments.count(*genericId)) {
                return genericAssignments[*genericId] == type2;
            } else {
                genericAssignments[*genericId] = type2;
                return true;
            }
        } else {
            return isSubtype(type2, type1);
        }
    };

    auto argIndex = 1;
    auto arg = argTypes.begin();
    auto expectedArg = expectedArgTypes.begin();
    for (;
        expectedArg != expectedArgTypes.end() && arg != argTypes.end();
        arg++, expectedArg++, argIndex++)
    {
        if (!unify(*expectedArg, *arg)) {
            THROW_ERROR_EXCEPTION(
                "Wrong type for argument %v to function %Qv: expected %Qv, got %Qv",
                functionName,
                argIndex,
                TypeToString(*expectedArg, genericAssignments),
                TypeToString(*arg, genericAssignments))
                << TErrorAttribute("expression", source);
        }
    }

    bool hasNoRepeatedArgument = repeatedArgType.Is<EValueType>() &&
        repeatedArgType.As<EValueType>() == EValueType::Null;

    if (expectedArg != expectedArgTypes.end() ||
        (arg != argTypes.end() && hasNoRepeatedArgument))
    {
        THROW_ERROR_EXCEPTION(
            "Wrong number of arguments to function %Qv: expected %v, got %v",
            functionName,
            expectedArgTypes.size(),
            argTypes.size())
            << TErrorAttribute("expression", source);
    }

    for (; arg != argTypes.end(); arg++) {
        if (!unify(repeatedArgType, *arg)) {
            THROW_ERROR_EXCEPTION(
                "Wrong type for repeated argument to function %Qv: expected %Qv, got %Qv",
                functionName,
                TypeToString(repeatedArgType, genericAssignments),
                TypeToString(*arg, genericAssignments))
                << TErrorAttribute("expression", source);
        }
    }

    if (auto* genericResult = resultType.TryAs<TTypeArgument>()) {
        if (!genericAssignments.count(*genericResult)) {
            THROW_ERROR_EXCEPTION(
                "Ambiguous result type for function %Qv",
                functionName)
                << TErrorAttribute("expression", source);
        }
        return genericAssignments[*genericResult];
    } else if (!resultType.TryAs<EValueType>()) {
        THROW_ERROR_EXCEPTION(
            "Ambiguous result type for function %Qv",
            functionName)
            << TErrorAttribute("expression", source);
    } else {
        return resultType.As<EValueType>();
    }

    return EValueType::Null;
}

////////////////////////////////////////////////////////////////////////////////

TKeyTriePtr TUniversalRangeFunction::ExtractKeyRange(
    const TIntrusivePtr<const TFunctionExpression>& expr,
    const TKeyColumns& keyColumns,
    const TRowBufferPtr& rowBuffer) const
{
    return TKeyTrie::Universal();
}

////////////////////////////////////////////////////////////////////////////////

TCodegenExpression TCodegenFunction::MakeCodegenExpr(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name) const
{
    return [
        this,
        MOVE(codegenArgs),
        type,
        name
    ] (TCGContext& builder, Value* row) {
        return CodegenValue(
            codegenArgs,
            type,
            name,
            builder,
            row);
    };
}

////////////////////////////////////////////////////////////////////////////////

TIfFunction::TIfFunction() : TTypedFunction(
    "if",
    std::vector<TType>{ EValueType::Boolean, 0, 0 },
    0)
{ }

TCGValue TIfFunction::CodegenValue(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name,
    TCGContext& builder,
    Value* row) const
{
    auto nameTwine = Twine(name.c_str());

    YCHECK(codegenArgs.size() == 3);
    auto condition = codegenArgs[0](builder, row);
    YCHECK(condition.GetStaticType() == EValueType::Boolean);

    return CodegenIf<TCGContext, TCGValue>(
        builder,
        condition.IsNull(),
        [&] (TCGContext& builder) {
            return TCGValue::CreateNull(builder, type);
        },
        [&] (TCGContext& builder) {
            return CodegenIf<TCGContext, TCGValue>(
                builder,
                builder.CreateICmpNE(
                    builder.CreateZExtOrBitCast(condition.GetData(), builder.getInt64Ty()),
                    builder.getInt64(0)),
                [&] (TCGContext& builder) {
                    return codegenArgs[1](builder, row);
                },
                [&] (TCGContext& builder) {
                    return codegenArgs[2](builder, row);
                });
        },
        nameTwine);
}

////////////////////////////////////////////////////////////////////////////////

TIsPrefixFunction::TIsPrefixFunction()
    : TTypedFunction(
      "is_prefix",
      std::vector<TType>{ EValueType::String, EValueType::String },
      EValueType::Boolean)
{ }

TCGValue TIsPrefixFunction::CodegenValue(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name,
    TCGContext& builder,
    Value* row) const
{
    return MakeBinaryFunctionCall("IsPrefix", codegenArgs, type, name, builder, row);
}

TKeyTriePtr TIsPrefixFunction::ExtractKeyRange(
    const TIntrusivePtr<const TFunctionExpression>& expr,
    const TKeyColumns& keyColumns,
    const TRowBufferPtr& rowBuffer) const
{
    auto result = TKeyTrie::Universal();
    auto lhsExpr = expr->Arguments[0];
    auto rhsExpr = expr->Arguments[1];

    auto referenceExpr = rhsExpr->As<TReferenceExpression>();
    auto constantExpr = lhsExpr->As<TLiteralExpression>();

    if (referenceExpr && constantExpr) {
        int keyPartIndex = ColumnNameToKeyPartIndex(keyColumns, referenceExpr->ColumnName);
        if (keyPartIndex >= 0) {
            auto value = TValue(constantExpr->Value);

            YCHECK(value.Type == EValueType::String);

            result = New<TKeyTrie>(keyPartIndex);
            result->Bounds.emplace_back(value, true);

            ui32 length = value.Length;
            while (length > 0 && value.Data.String[length - 1] == std::numeric_limits<char>::max()) {
                --length;
            }

            if (length > 0) {
                char* newValue = rowBuffer->GetPool()->AllocateUnaligned(length);
                memcpy(newValue, value.Data.String, length);
                ++newValue[length - 1];

                value.Length = length;
                value.Data.String = newValue;
            } else {
                value = MakeSentinelValue<TUnversionedValue>(EValueType::Max);
            }
            result->Bounds.emplace_back(value, false);
        }
    }

    return result;
}

////////////////////////////////////////////////////////////////////////////////

TCastFunction::TCastFunction(
    EValueType resultType,
    const Stroka& functionName)
    : TTypedFunction(
        functionName,
        std::vector<TType>{ CastTypes_ },
        resultType)
{ }

const TUnionType TCastFunction::CastTypes_ = TUnionType{
    EValueType::Int64,
    EValueType::Uint64,
    EValueType::Double};

TCGValue TCastFunction::CodegenValue(
    std::vector<TCodegenExpression> codegenArgs,
    EValueType type,
    const Stroka& name,
    TCGContext& builder,
    Value* row) const
{
    YCHECK(codegenArgs.size() == 1);
    return codegenArgs[0](builder, row).Cast(builder, type);
}

////////////////////////////////////////////////////////////////////////////////

TAggregateFunction::TAggregateFunction(
    Stroka name)
    : Name_(name)
{ }

Stroka TAggregateFunction::GetName() const
{
    return Name_;
}

TCodegenAggregateInit MakeCodegenInitialize(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* aggState) {
        builder.CreateStore(
            builder.getInt16(static_cast<ui16>(EValueType::Null)),
            builder.CreateStructGEP(
                aggState,
                TTypeBuilder::Type));
    };
}

TCodegenAggregateUpdate MakeCodegenUpdate(
    const Stroka& aggregateFunction,
    EValueType type,
    const Stroka& nameStroka)
{
    return [
            aggregateFunction,
            type,
            MOVE(nameStroka)
        ] (TCGContext& builder, Value* aggregateStatePtr, Value* newValuePtr) {
            Twine name = nameStroka.c_str();

            auto aggregateValue = TCGValue::CreateFromLlvmValue(
                builder,
                aggregateStatePtr,
                type,
                name + ".aggregate");
            auto newValue = TCGValue::CreateFromLlvmValue(
                builder,
                newValuePtr,
                type);

            CodegenIf<TCGContext>(
                builder,
                newValue.IsNull(),
                [&] (TCGContext& builder) { },
                [&] (TCGContext& builder) {

                    CodegenIf<TCGContext>(
                        builder,
                        aggregateValue.IsNull(),
                        [&] (TCGContext& builder) {
                            newValue.StoreToValue(builder, aggregateStatePtr);
                        },
                        [&] (TCGContext& builder) {
                            Value* newData = newValue.GetData();
                            Value* aggregateData = aggregateValue.GetData();
                            Value* resultData = nullptr;

                            // TODO(lukyan): support other types

                            if (aggregateFunction == "sum") {
                                switch (type) {
                                    case EValueType::Int64:
                                    case EValueType::Uint64:
                                        resultData = builder.CreateAdd(
                                            aggregateData,
                                            newData);
                                        break;
                                    case EValueType::Double:
                                        resultData = builder.CreateFAdd(
                                            aggregateData,
                                            newData);
                                        break;
                                    default:
                                        YUNIMPLEMENTED();
                                }
                            } else if (aggregateFunction == "min") {
                                Value* compareResult = nullptr;
                                switch (type) {
                                    case EValueType::Int64:
                                        compareResult = builder.CreateICmpSLE(aggregateData, newData);
                                        break;
                                    case EValueType::Uint64:
                                        compareResult = builder.CreateICmpULE(aggregateData, newData);
                                        break;
                                    case EValueType::Double:
                                        compareResult = builder.CreateFCmpULE(aggregateData, newData);
                                        break;
                                    default:
                                        YUNIMPLEMENTED();
                                }

                                resultData = builder.CreateSelect(
                                    compareResult,
                                    aggregateData,
                                    newData);
                            } else if (aggregateFunction == "max") {
                                Value* compareResult = nullptr;
                                switch (type) {
                                    case EValueType::Int64:
                                        compareResult = builder.CreateICmpSGE(aggregateData, newData);
                                        break;
                                    case EValueType::Uint64:
                                        compareResult = builder.CreateICmpUGE(aggregateData, newData);
                                        break;
                                    case EValueType::Double:
                                        compareResult = builder.CreateFCmpUGE(aggregateData, newData);
                                        break;
                                    default:
                                        YUNIMPLEMENTED();
                                }

                                resultData = builder.CreateSelect(
                                    compareResult,
                                    aggregateData,
                                    newData);
                            } else {
                                YUNIMPLEMENTED();
                            }

                            auto result = TCGValue::CreateFromValue(
                                builder,
                                nullptr,
                                nullptr,
                                resultData,
                                type,
                                "result");
                            result.StoreToValue(builder, aggregateStatePtr);
                        });

                });
        };
}

TCodegenAggregateMerge MakeCodegenMerge(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* dstAggState, Value* aggState) {
    };
}

TCodegenAggregateFinalize MakeCodegenFinalize(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* result, Value* aggState) {
        builder.CreateStore(
            builder.CreateLoad(
                builder.CreateStructGEP(aggState, TTypeBuilder::Type)),
            builder.CreateStructGEP(result, TTypeBuilder::Type));
        builder.CreateStore(
            builder.CreateLoad(
                builder.CreateStructGEP(aggState, TTypeBuilder::Data)),
            builder.CreateStructGEP(result, TTypeBuilder::Data));
    };
}

const TCodegenAggregate TAggregateFunction::MakeCodegenAggregate(
    EValueType type,
    const Stroka& name) const
{
    TCodegenAggregate codegenAggregate;
    codegenAggregate.Initialize = MakeCodegenInitialize(type, name);
    codegenAggregate.Update = MakeCodegenUpdate(GetName(), type, name);
    codegenAggregate.Merge = MakeCodegenMerge(type, name);
    codegenAggregate.Finalize = MakeCodegenFinalize(type, name);
    return codegenAggregate;
}

std::vector<TColumnSchema> TAggregateFunction::GetStateSchema(
    const Stroka& name,
    EValueType type) const
{
    return std::vector<TColumnSchema>{
        TColumnSchema(name, type)
    };
}

EValueType TAggregateFunction::InferResultType(
    EValueType argumentType,
    const TStringBuf& source) const
{
    if (argumentType == EValueType::Int64
        || argumentType == EValueType::Uint64
        || argumentType == EValueType::Double) {
        return argumentType;
    }
    THROW_ERROR_EXCEPTION(
        "Aggregate function %Qv must have an integer or double argument",
        GetName());
}

////////////////////////////////////////////////////////////////////////////////

TAverageAggregateFunction::TAverageAggregateFunction()
{ }

Stroka TAverageAggregateFunction::GetName() const
{
    return "avg";
}

TCodegenAggregateInit MakeCodegenAverageInitialize(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* aggState) {
        auto sumValue = aggState;
        auto countValue = builder.CreateConstInBoundsGEP1_32(aggState, 1);

        builder.CreateStore(
            builder.getInt16(static_cast<ui16>(EValueType::Int64)),
            builder.CreateStructGEP(sumValue, TTypeBuilder::Type));
        builder.CreateStore(
            builder.getInt16(static_cast<ui16>(EValueType::Int64)),
            builder.CreateStructGEP(countValue, TTypeBuilder::Type));

        builder.CreateStore(
            builder.getInt64(0),
            builder.CreateStructGEP(sumValue, TTypeBuilder::Data));
        builder.CreateStore(
            builder.getInt64(0),
            builder.CreateStructGEP(countValue, TTypeBuilder::Data));
    };
}

TCodegenAggregateUpdate MakeCodegenAverageUpdate(
    const Stroka& aggregateFunction,
    EValueType type,
    const Stroka& nameStroka)
{
    return [
            aggregateFunction,
            type,
            MOVE(nameStroka)
        ] (TCGContext& builder, Value* aggState, Value* newValue) {
            auto sumValue = aggState;
            auto countValue = builder.CreateConstInBoundsGEP1_32(aggState, 1);

            auto resultSum = builder.CreateAdd(
                builder.CreateLoad(
                    builder.CreateStructGEP(sumValue, TTypeBuilder::Data)),
                builder.CreateLoad(
                    builder.CreateStructGEP(newValue, TTypeBuilder::Data)));

            auto resultCount = builder.CreateAdd(
                builder.CreateLoad(
                    builder.CreateStructGEP(countValue, TTypeBuilder::Data)),
                builder.getInt64(1));

            builder.CreateStore(
                resultSum,
                builder.CreateStructGEP(sumValue, TTypeBuilder::Data));

            builder.CreateStore(
                resultCount,
                builder.CreateStructGEP(countValue, TTypeBuilder::Data));
        };
}

TCodegenAggregateMerge MakeCodegenAverageMerge(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* dstAggState, Value* aggState) {
        auto dstSumValue = dstAggState;
        auto dstCountValue = builder.CreateConstInBoundsGEP1_32(dstAggState, 1);

        auto sumValue = aggState;
        auto countValue = builder.CreateConstInBoundsGEP1_32(aggState, 1);

        auto resultSum = builder.CreateAdd(
            builder.CreateLoad(
                builder.CreateStructGEP(dstSumValue, TTypeBuilder::Data)),
            builder.CreateLoad(
                builder.CreateStructGEP(sumValue, TTypeBuilder::Data)));

        auto resultCount = builder.CreateAdd(
            builder.CreateLoad(
                builder.CreateStructGEP(dstCountValue, TTypeBuilder::Data)),
            builder.CreateLoad(
                builder.CreateStructGEP(countValue, TTypeBuilder::Data)));

        builder.CreateStore(
            resultSum,
            builder.CreateStructGEP(dstSumValue, TTypeBuilder::Data));

        builder.CreateStore(
            resultCount,
            builder.CreateStructGEP(dstCountValue, TTypeBuilder::Data));
    };
}

TCodegenAggregateFinalize MakeCodegenAverageFinalize(
    EValueType type,
    const Stroka& name)
{
    return [
    ] (TCGContext& builder, Value* result, Value* aggState) {
        auto sumValue = builder.CreateLoad(
            builder.CreateStructGEP(aggState, TTypeBuilder::Data));
        auto countValue = builder.CreateLoad(
            builder.CreateStructGEP(
                builder.CreateConstInBoundsGEP1_32(aggState, 1),
                TTypeBuilder::Data));

        auto resultValue = builder.CreateFDiv(
            builder.CreateSIToFP(sumValue, builder.getDoubleTy()),
            builder.CreateSIToFP(countValue, builder.getDoubleTy()));

        auto dataType = TDataTypeBuilder::get(builder.getContext());
        auto resultData = builder.CreateBitCast(
            resultValue,
            dataType);

        builder.CreateStore(
            builder.getInt16(static_cast<ui16>(EValueType::Double)),
            builder.CreateStructGEP(result, TTypeBuilder::Type));
        builder.CreateStore(
            resultData,
            builder.CreateStructGEP(result, TTypeBuilder::Data));

    };
}

const TCodegenAggregate TAverageAggregateFunction::MakeCodegenAggregate(
    EValueType type,
    const Stroka& name) const
{
    TCodegenAggregate codegenAggregate;
    codegenAggregate.Initialize = MakeCodegenAverageInitialize(type, name);
    codegenAggregate.Update = MakeCodegenAverageUpdate(GetName(), type, name);
    codegenAggregate.Merge = MakeCodegenAverageMerge(type, name);
    codegenAggregate.Finalize = MakeCodegenAverageFinalize(type, name);
    return codegenAggregate;
}

std::vector<TColumnSchema> TAverageAggregateFunction::GetStateSchema(
    const Stroka& name,
    EValueType type) const
{
    return std::vector<TColumnSchema>{
        TColumnSchema("sum", EValueType::Int64),
        TColumnSchema("count", EValueType::Int64)
    };
}

EValueType TAverageAggregateFunction::InferResultType(
    EValueType argumentType,
    const TStringBuf& source) const
{
    if (argumentType != EValueType::Int64) {
        THROW_ERROR_EXCEPTION(
            "Aggregate function %Qv must have a signed integer argument",
            GetName());
    }
    return EValueType::Double;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NQueryClient
} // namespace NYT
