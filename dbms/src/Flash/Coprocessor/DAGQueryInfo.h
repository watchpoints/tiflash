// Copyright 2022 PingCAP, Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Interpreters/TimezoneInfo.h>
#include <Storages/Transaction/DecodingStorageSchemaSnapshot.h>
#include <google/protobuf/repeated_ptr_field.h>
#include <tipb/expression.pb.h>


namespace DB
{
// DAGQueryInfo contains filter information in dag request, it will
// be used to extracted key conditions by storage engine
struct DAGQueryInfo
{
    DAGQueryInfo(
        const google::protobuf::RepeatedPtrField<tipb::Expr> & filters_,
        const google::protobuf::RepeatedPtrField<tipb::Expr> & pushed_down_filters_,
        const ColumnInfos & source_columns_,
        const std::vector<int> & runtime_filter_ids_,
        const int rf_max_wait_time_ms_,
        const TimezoneInfo & timezone_info_)
        : source_columns(source_columns_)
        , filters(filters_)
        , pushed_down_filters(pushed_down_filters_)
        , runtime_filter_ids(runtime_filter_ids_)
        , rf_max_wait_time_ms(rf_max_wait_time_ms_)
        , timezone_info(timezone_info_){};

    // A light copy of tipb::TableScan::columns from TiDB, some attributes are empty, like name.
    const ColumnInfos & source_columns;
    // filters in dag request
    const google::protobuf::RepeatedPtrField<tipb::Expr> & filters;
    // filters have been push down to storage engine in dag request
    const google::protobuf::RepeatedPtrField<tipb::Expr> & pushed_down_filters;

    const std::vector<int> & runtime_filter_ids;
    const int rf_max_wait_time_ms;

    const TimezoneInfo & timezone_info;
};
} // namespace DB
