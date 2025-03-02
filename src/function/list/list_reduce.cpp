#include "common/exception/binder.h"
#include "common/exception/runtime.h"
#include "expression_evaluator/lambda_evaluator.h"
#include "function/list/vector_list_functions.h"
#include "function/scalar_function.h"

namespace kuzu {
namespace function {

using namespace kuzu::common;

static std::unique_ptr<FunctionBindData> bindFunc(const ScalarBindFuncInput& input) {
    if (input.arguments[1]->expressionType != ExpressionType::LAMBDA) {
        throw BinderException(stringFormat(
            "The second argument of LIST_REDUCE should be a lambda expression but got {}.",
            ExpressionTypeUtil::toString(input.arguments[1]->expressionType)));
    }
    std::vector<LogicalType> paramTypes;
    paramTypes.push_back(input.arguments[0]->getDataType().copy());
    paramTypes.push_back(input.arguments[1]->getDataType().copy());
    return std::make_unique<FunctionBindData>(std::move(paramTypes),
        ListType::getChildType(input.arguments[0]->getDataType()).copy());
}

static void reduceList(const list_entry_t& listEntry, uint64_t pos, common::ValueVector& result,
    common::SelectionVector* resultSelVector, common::ValueVector& inputDataVector,
    common::ValueVector& tmpResultVector, common::SelectionVector* tmpResultVectorSelVector,
    evaluator::ListLambdaBindData& bindData) {
    const auto& paramIndices = bindData.paramIndices;
    std::vector<ValueVector*> params(bindData.lambdaParamEvaluators.size());
    for (auto i = 0u; i < bindData.lambdaParamEvaluators.size(); i++) {
        auto param = bindData.lambdaParamEvaluators[i]->resultVector.get();
        params[i] = param;
    }
    auto paramPos = params[0]->state->getSelVector()[0];
    auto tmpResultPos = (*tmpResultVectorSelVector)[0];
    if (listEntry.size == 0) {
        throw common::RuntimeException{"Cannot execute list_reduce on an empty list."};
    }
    if (listEntry.size == 1) {
        result.copyFromVectorData((*resultSelVector)[pos], &inputDataVector, listEntry.offset);
    } else {
        for (auto j = 0u; j < listEntry.size - 1; j++) {
            for (auto k = 0u; k < params.size(); k++) {
                if (0u == paramIndices[k] && 0u != j) {
                    params[k]->copyFromVectorData(paramPos, &tmpResultVector, tmpResultPos);
                } else {
                    params[k]->copyFromVectorData(paramPos, &inputDataVector,
                        listEntry.offset + j + paramIndices[k]);
                }
                params[k]->state->getSelVectorUnsafe().setSelSize(1);
            }
            bindData.rootEvaluator->evaluate();
        }
        result.copyFromVectorData((*resultSelVector)[pos], &tmpResultVector, tmpResultPos);
    }
}

static void execFunc(const std::vector<std::shared_ptr<common::ValueVector>>& input,
    const std::vector<common::SelectionVector*>& inputSelVectors, common::ValueVector& result,
    common::SelectionVector* resultSelVector, void* bindData) {
    KU_ASSERT(input.size() == 2);
    auto listLambdaBindData = reinterpret_cast<evaluator::ListLambdaBindData*>(bindData);
    const auto* inputVector = input[0].get();
    for (auto i = 0u; i < inputSelVectors[0]->getSelSize(); i++) {
        auto listEntry = inputVector->getValue<list_entry_t>((*inputSelVectors[0])[i]);
        reduceList(listEntry, i, result, resultSelVector, *ListVector::getDataVector(inputVector),
            *input[1].get(), inputSelVectors[1], *listLambdaBindData);
    }
}

function_set ListReduceFunction::getFunctionSet() {
    function_set result;
    auto function = std::make_unique<ScalarFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::LIST, LogicalTypeID::ANY}, LogicalTypeID::LIST,
        execFunc);
    function->bindFunc = bindFunc;
    function->isListLambda = true;
    result.push_back(std::move(function));
    return result;
}

} // namespace function
} // namespace kuzu
