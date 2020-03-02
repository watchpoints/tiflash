#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <tipb/select.pb.h>
#pragma GCC diagnostic pop

#include "DAGQueryBlock.h"
#include "DAGUtils.h"

namespace DB
{

namespace ErrorCodes
{
extern const int NOT_IMPLEMENTED;
extern const int LOGICAL_ERROR;
extern const int COP_BAD_DAG_REQUEST;
} // namespace ErrorCodes

class Context;
class TiFlashMetrics;
using TiFlashMetricsPtr = std::shared_ptr<TiFlashMetrics>;

bool isSourceNode(const tipb::Executor * root)
{
    return root->tp() == tipb::ExecType::TypeJoin || root->tp() == tipb::ExecType::TypeTableScan;
}

const static String SOURCE_NAME("source");
const static String SEL_NAME("selection");
const static String AGG_NAME("aggregation");
const static String TOPN_NAME("topN");
const static String LIMIT_NAME("limit");

static void assignOrThrowException(const tipb::Executor ** to, const tipb::Executor * from, const String & name)
{
    if (*to != nullptr)
    {
        throw Exception("Duplicated " + name + " in DAG request");
    }
    *to = from;
}

void collectOutPutFieldTypesFromAgg(std::vector<tipb::FieldType> & field_type, const tipb::Aggregation & agg)
{
    for (auto & expr : agg.agg_func())
    {
        if (!exprHasValidFieldType(expr))
        {
            throw Exception("Agg expression without valid field type", ErrorCodes::COP_BAD_DAG_REQUEST);
        }
        field_type.push_back(expr.field_type());
    }
    for (auto & expr : agg.group_by())
    {
        if (!exprHasValidFieldType(expr))
        {
            throw Exception("Group by expression without valid field type", ErrorCodes::COP_BAD_DAG_REQUEST);
        }
        field_type.push_back(expr.field_type());
    }
}

DAGQueryBlock::DAGQueryBlock(const tipb::Executor * root)
{
    const tipb::Executor * current = root;
    while (isSourceNode(current))
    {
        switch (current->tp())
        {
            case tipb::ExecType::TypeSelection:
                assignOrThrowException(&selection, current, SEL_NAME);
                current = &current->selection().child();
                break;
            case tipb::ExecType::TypeAggregation:
                assignOrThrowException(&aggregation, current, AGG_NAME);
                collectOutPutFieldTypesFromAgg(output_field_types, current->aggregation());
                current = &current->aggregation().child();
                break;
            case tipb::ExecType::TypeStreamAgg:
                assignOrThrowException(&aggregation, current, AGG_NAME);
                collectOutPutFieldTypesFromAgg(output_field_types, current->stream_agg());
                current = &current->stream_agg().child();
                break;
            case tipb::ExecType::TypeLimit:
                assignOrThrowException(&limitOrTopN, current, LIMIT_NAME);
                current = &current->limit().child();
                break;
            case tipb::ExecType::TypeTopN:
                assignOrThrowException(&limitOrTopN, current, TOPN_NAME);
                current = &current->topn().child();
                break;
            case tipb::ExecType::TypeIndexScan:
                throw Exception("Unsupported executor in DAG request: " + current->DebugString(), ErrorCodes::NOT_IMPLEMENTED);
            default:
                throw Exception("Should not reach here", ErrorCodes::LOGICAL_ERROR);
        }
    }
    assignOrThrowException(&source, current, SOURCE_NAME);
    if (source->tp() == tipb::ExecType::TypeJoin)
    {
        // todo need to figure out left and right side of the join
        children.push_back(std::make_shared<DAGQueryBlock>(&source->join().probe_exec()));
        children.push_back(std::make_shared<DAGQueryBlock>(&source->join().build_exec()));
        if (output_field_types.empty())
        {
            for (auto & field_type : children[0]->output_field_types)
                output_field_types.push_back(field_type);
            for (auto & field_type : children[1]->output_field_types)
                output_field_types.push_back(field_type);
        }
    }
    else
    {
        if (output_field_types.empty())
        {
            for (auto & ci : source->tbl_scan().columns())
            {
                tipb::FieldType field_type;
                field_type.set_tp(ci.tp());
                field_type.set_flag(ci.flag());
                field_type.set_flen(ci.columnlen());
                field_type.set_decimal(ci.decimal());
                output_field_types.push_back(field_type);
            }
        }
    }
}

DAGQueryBlock::DAGQueryBlock(std::vector<const tipb::Executor *> & executors)
{
    for (size_t i = 0; i < executors.size(); i++)
    {
        switch (executors[i]->tp())
        {
            case tipb::ExecType::TypeTableScan:
                assignOrThrowException(&source, executors[i], SOURCE_NAME);
                break;
            case tipb::ExecType::TypeSelection:
                assignOrThrowException(&selection, executors[i], SEL_NAME);
                break;
            case tipb::ExecType::TypeStreamAgg:
                assignOrThrowException(&aggregation, executors[i], AGG_NAME);
                collectOutPutFieldTypesFromAgg(output_field_types, executors[i]->stream_agg());
                break;
            case tipb::ExecType::TypeAggregation:
                assignOrThrowException(&aggregation, executors[i], AGG_NAME);
                collectOutPutFieldTypesFromAgg(output_field_types, executors[i]->aggregation());
                break;
            case tipb::ExecType::TypeTopN:
                assignOrThrowException(&limitOrTopN, executors[i], TOPN_NAME);
                break;
            case tipb::ExecType::TypeLimit:
                assignOrThrowException(&limitOrTopN, executors[i], LIMIT_NAME);
                break;
            default:
                throw Exception("Unsupported executor in DAG request: " + executors[i]->DebugString(), ErrorCodes::NOT_IMPLEMENTED);
        }
    }
    if (output_field_types.empty())
    {
        for (auto & ci : source->tbl_scan().columns())
        {
            tipb::FieldType field_type;
            field_type.set_tp(ci.tp());
            field_type.set_flag(ci.flag());
            field_type.set_flen(ci.columnlen());
            field_type.set_decimal(ci.decimal());
            output_field_types.push_back(field_type);
        }
    }
}

} // namespace DB