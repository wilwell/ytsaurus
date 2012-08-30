#include "stdafx.h"
#include "helpers.h"
#include "operation.h"
#include "job.h"
#include "exec_node.h"
#include "operation_controller.h"
#include "job_resources.h"

#include <ytlib/ytree/fluent.h>

namespace NYT {
namespace NScheduler {

using namespace NYTree;

////////////////////////////////////////////////////////////////////

void BuildOperationAttributes(TOperationPtr operation, IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("operation_type").Scalar(CamelCaseToUnderscoreCase(operation->GetType().ToString()))
        .Item("transaction_id").Scalar(operation->GetTransactionId())
        .Item("state").Scalar(FormatEnum(operation->GetState()))
        .Item("start_time").Scalar(operation->GetStartTime())
        .Item("progress").BeginMap().EndMap()
        .Item("spec").Node(operation->GetSpec());
}

void BuildJobAttributes(TJobPtr job, NYTree::IYsonConsumer* consumer)
{
    auto state = job->GetState();
    auto error = FromProto(job->Result().error());
    BuildYsonMapFluently(consumer)
        .Item("job_type").Scalar(FormatEnum(job->GetType()))
        .Item("state").Scalar(FormatEnum(state))
        .Item("address").Scalar(job->GetNode()->GetAddress())
        .DoIf(state == EJobState::Failed, [=] (TFluentMap fluent) {
            fluent.Item("error").Scalar(error);
        });
}

void BuildExecNodeAttributes(TExecNodePtr node, IYsonConsumer* consumer)
{
    BuildYsonMapFluently(consumer)
        .Item("resource_utilization").Do(BIND(&BuildNodeResourcesYson, node->ResourceUtilization()))
        .Item("resource_limits").Do(BIND(&BuildNodeResourcesYson, node->ResourceLimits()))
        .Item("job_count").Scalar(static_cast<int>(node->Jobs().size()));
}

////////////////////////////////////////////////////////////////////

} // namespace NScheduler
} // namespace NYT

