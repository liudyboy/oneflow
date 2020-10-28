/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/framework/framework.h"

namespace oneflow {

REGISTER_CPU_ONLY_USER_OP("onerec_decoder")
    .Input("in")
    .Output("out")
    .Attr("key", UserOpAttrType::kAtString)
    .Attr("data_type", UserOpAttrType::kAtDataType)
    .Attr("static_shape", UserOpAttrType::kAtShape)
    .Attr<bool>("is_dynamic", UserOpAttrType::kAtBool, false)
    .Attr<bool>("has_reshape", UserOpAttrType::kAtBool, false)
    .Attr("reshape", UserOpAttrType::kAtShape)
    .Attr<bool>("has_batch_padding", UserOpAttrType::kAtBool, false)
    .Attr("batch_padding", UserOpAttrType::kAtShape)
    .SetTensorDescInferFn([](user_op::InferContext* ctx) -> Maybe<void> {
      user_op::TensorDesc* in_tensor = ctx->TensorDesc4ArgNameAndIndex("in", 0);
      user_op::TensorDesc* out_tensor = ctx->TensorDesc4ArgNameAndIndex("out", 0);
      CHECK_OR_RETURN(in_tensor->data_type() == DataType::kTensorBuffer);
      CHECK_OR_RETURN(in_tensor->shape().NumAxes() == 1 && in_tensor->shape().At(0) >= 1);
      const Shape& static_shape = ctx->Attr<Shape>("static_shape");
      DimVector dim_vec(1 + static_shape.NumAxes());
      dim_vec[0] = in_tensor->shape().At(0);
      FOR_RANGE(int64_t, i, 1, dim_vec.size()) { dim_vec[i] = static_shape.At(i - 1); }
      *out_tensor->mut_shape() = Shape(dim_vec);
      *out_tensor->mut_data_type() = ctx->Attr<DataType>("data_type");
      out_tensor->set_is_dynamic(ctx->Attr<bool>("is_dynamic"));
      return Maybe<void>::Ok();
    })
    .SetInputArgModifyFn([](user_op::GetInputArgModifier GetInputArgModifierFn,
                            const user_op::UserOpConfWrapper&) {
      user_op::InputArgModifier* in_modifier = GetInputArgModifierFn("in", 0);
      CHECK_NOTNULL(in_modifier);
      in_modifier->set_requires_grad(false);
    })
    .SetGetSbpFn([](user_op::SbpContext* ctx) -> Maybe<void> {
      ctx->NewBuilder()
          .Split(user_op::OpArg("in", 0), 0)
          .Split(user_op::OpArg("out", 0), 0)
          .Build();
      return Maybe<void>::Ok();
    })
    .SetBatchAxisInferFn([](user_op::BatchAxisContext* ctx) -> Maybe<void> {
      CHECK_EQ_OR_RETURN(ctx->BatchAxis4ArgNameAndIndex("in", 0)->value(), 0);
      ctx->BatchAxis4ArgNameAndIndex("out", 0)->set_value(0);
      return Maybe<void>::Ok();
    })
    .SetOutputArgModifyFn([](user_op::GetOutputArgModifier GetOutputArgModifierFn,
                             const user_op::UserOpConfWrapper& conf) {
      user_op::OutputArgModifier* out_modifier = GetOutputArgModifierFn("out", 0);
      CHECK(out_modifier != nullptr);
      out_modifier->set_header_infered_before_compute(false);
    });
}
