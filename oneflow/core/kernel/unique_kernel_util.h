#ifndef ONEFLOW_CORE_KERNEL_UNIQUE_KERNEL_UTIL_H_
#define ONEFLOW_CORE_KERNEL_UNIQUE_KERNEL_UTIL_H_

#include "oneflow/core/kernel/kernel_util.h"

namespace oneflow {

template<DeviceType device_type, typename KEY, typename IDX>
struct UniqueKernelUtil {
  static void Unique(DeviceCtx* ctx, int64_t n, const KEY* in, IDX* num_unique, KEY* unique_out,
                     IDX* idx_out, void* workspace, int64_t workspace_size_in_bytes);
  static void GetWorkspaceSizeInBytes(DeviceCtx* ctx, int64_t n, int64_t* workspace_size_in_bytes);
};

#define UNIQUE_KERNEL_KV_DATA_TYPE_SEQ            \
  OF_PP_MAKE_TUPLE_SEQ(int32_t, DataType::kInt32) \
  OF_PP_MAKE_TUPLE_SEQ(int64_t, DataType::kInt64)

}  // namespace oneflow

#endif  // ONEFLOW_CORE_KERNEL_UNIQUE_KERNEL_UTIL_H_
