#include "oneflow/core/operator/operator.h"
#include "oneflow/core/operator/indexed_slices_reduce_sum_op_util.h"

namespace oneflow {

class IndexedSlicesLazyAdamOptimizerOp final : public Operator {
 public:
  OF_DISALLOW_COPY_AND_MOVE(IndexedSlicesLazyAdamOptimizerOp);
  IndexedSlicesLazyAdamOptimizerOp() = default;
  ~IndexedSlicesLazyAdamOptimizerOp() override = default;

 private:
  void InitFromOpConf() override;
  const PbMessage& GetCustomizedConf() const override;
  Maybe<void> InferBlobDescs(std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                             const ParallelContext* parallel_ctx) const override;
  Maybe<void> InferBatchAxis(
      std::function<OptInt64*(const std::string&)> BatchAxis4BnInOp) const override {
    return Maybe<void>::Ok();
  }
  Maybe<void> InferOutBlobDescs(std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                                const ParallelContext*, const SbpSignature* sbp_signature,
                                std::function<void(OpContext*)> EnrollOpCtx) const override {
    return Maybe<void>::Ok();
  }
  Maybe<void> GetSbpSignatures(
      const std::function<Maybe<const BlobDesc*>(const std::string&)>& LogicalBlobDesc4Ibn,
      SbpSignatureList* sbp_sig_list) const override;
  void VirtualGenKernelConf(
      std::function<const BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
      const ParallelContext* parallel_ctx, KernelConf* kernel_conf, const OpContext* op_ctx,
      std::function<const BlobDesc&(const std::string&)> LogicalBlobDesc4BnInOp) const override;
};

void IndexedSlicesLazyAdamOptimizerOp::InitFromOpConf() {
  const auto& conf = op_conf().indexed_slices_lazy_adam_optimizer_conf();
  CHECK_GE(conf.beta1(), 0);
  CHECK_LT(conf.beta1(), 1);
  CHECK_GE(conf.beta2(), 0);
  CHECK_LT(conf.beta2(), 1);

  EnrollInputBn("m", false)->set_is_mutable(true);
  EnrollInputBn("v", false)->set_is_mutable(true);
  EnrollInputBn("model_diff_indices", false);
  EnrollInputBn("model_diff_values", false);
  EnrollInputBn("model", false)->set_is_mutable(true);
  EnrollInputBn("train_step", false);
  EnrollInputBn("learning_rate", false);

  EnrollTmpBn("num_unique_diff_indices");
  EnrollTmpBn("unique_diff_indices");
  EnrollTmpBn("unique_diff_values");
  EnrollTmpBn("unique_workspace");
}

Maybe<void> IndexedSlicesLazyAdamOptimizerOp::InferBlobDescs(
    std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
    const ParallelContext* parallel_ctx) const {
  const BlobDesc* indices = GetBlobDesc4BnInOp("model_diff_indices");
  const BlobDesc* values = GetBlobDesc4BnInOp("model_diff_values");
  const int64_t num_indices_axes = indices->shape().NumAxes();
  CHECK_GT(values->shape().NumAxes(), num_indices_axes);
  FOR_RANGE(int64_t, i, 0, num_indices_axes) {
    CHECK_EQ(indices->shape().At(i), values->shape().At(i));
  }
  *GetBlobDesc4BnInOp("unique_diff_indices") = *indices;
  *GetBlobDesc4BnInOp("unique_diff_values") = *values;
  BlobDesc* num_unique_diff_indices = GetBlobDesc4BnInOp("num_unique_diff_indices");
  num_unique_diff_indices->set_data_type(DataType::kInt64);
  num_unique_diff_indices->mut_shape() = Shape({1});
  int64_t unique_workspace_size = 0;
  IndexedSlicesReduceSumOpUtil::GetWorkspaceSizeInBytes(
      device_type(), values->data_type(), indices->data_type(), indices->shape().elem_cnt(),
      values->shape().Count(num_indices_axes), &unique_workspace_size);
  BlobDesc* unique_workspace = GetBlobDesc4BnInOp("unique_workspace");
  unique_workspace->set_data_type(DataType::kChar);
  unique_workspace->mut_shape() = Shape({unique_workspace_size});
  return Maybe<void>::Ok();
}

Maybe<void> IndexedSlicesLazyAdamOptimizerOp::GetSbpSignatures(
    const std::function<Maybe<const BlobDesc*>(const std::string&)>& LogicalBlobDesc4Ibn,
    SbpSignatureList* sbp_sig_list) const {
  SbpSignatureBuilder()
      .Split("m", 0)
      .Split("v", 0)
      .Split("model", 0)
      .Broadcast("model_diff_indices")
      .Broadcast("model_diff_values")
      .Broadcast("train_step")
      .Broadcast("learning_rate")
      .Build(sbp_sig_list->mutable_sbp_signature()->Add());
  return Maybe<void>::Ok();
}

const PbMessage& IndexedSlicesLazyAdamOptimizerOp::GetCustomizedConf() const {
  return op_conf().indexed_slices_lazy_adam_optimizer_conf();
}

void IndexedSlicesLazyAdamOptimizerOp::VirtualGenKernelConf(
    std::function<const BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
    const ParallelContext* parallel_ctx, KernelConf* kernel_conf, const OpContext* op_ctx,
    std::function<const BlobDesc&(const std::string&)> LogicalBlobDesc4BnInOp) const {
  kernel_conf->set_data_type(GetBlobDesc4BnInOp("model")->data_type());
  kernel_conf->mutable_indexed_slices_lazy_adam_optimizer_conf()->set_indices_data_type(
      GetBlobDesc4BnInOp("model_diff_indices")->data_type());
}

REGISTER_OP(OperatorConf::kIndexedSlicesLazyAdamOptimizerConf, IndexedSlicesLazyAdamOptimizerOp);

}  // namespace oneflow
