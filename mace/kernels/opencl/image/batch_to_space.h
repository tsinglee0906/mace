// Copyright 2018 Xiaomi, Inc.  All rights reserved.
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
#ifndef MACE_KERNELS_OPENCL_IMAGE_BATCH_TO_SPACE_H_
#define MACE_KERNELS_OPENCL_IMAGE_BATCH_TO_SPACE_H_

#include "mace/kernels/batch_to_space.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "mace/kernels/opencl/helper.h"

namespace mace {
namespace kernels {
namespace opencl {
namespace image {

template <typename T>
class BatchToSpaceKernel : public OpenCLBatchToSpaceKernel {
 public:
  MaceStatus Compute(
      OpKernelContext *context,
      const Tensor *batch_tensor,
      const std::vector<int> &paddings,
      const std::vector<int> &block_shape,
      const std::vector<index_t> &output_shape,
      Tensor *space_tensor,
      StatsFuture *future) override;

 private:
  cl::Kernel kernel_;
  uint32_t kwg_size_;
  std::vector<index_t> input_shape_;
};

template <typename T>
MaceStatus BatchToSpaceKernel<T>::Compute(
    OpKernelContext *context,
    const Tensor *batch_tensor,
    const std::vector<int> &paddings,
    const std::vector<int> &block_shape,
    const std::vector<index_t> &output_shape,
    Tensor *space_tensor,
    StatsFuture *future) {
  std::vector<size_t> output_image_shape;
  CalImage2DShape(output_shape, BufferType::IN_OUT_CHANNEL,
                  &output_image_shape);
  MACE_RETURN_IF_ERROR(
      space_tensor->ResizeImage(output_shape, output_image_shape));

  const uint32_t chan_blk =
      static_cast<uint32_t>(RoundUpDiv4(batch_tensor->dim(3)));

  const uint32_t gws[3] = {
      chan_blk, static_cast<uint32_t>(batch_tensor->dim(2)),
      static_cast<uint32_t>(batch_tensor->dim(0) * batch_tensor->dim(1))};

  auto runtime = context->device()->opencl_runtime();
  MACE_OUT_OF_RANGE_DEFINITION;

  if (kernel_.get() == nullptr) {
    const char *kernel_name = "batch_to_space";
    std::string obfuscated_kernel_name = MACE_OBFUSCATE_SYMBOL(kernel_name);
    std::set<std::string> built_options;
    MACE_OUT_OF_RANGE_CONFIG;
    MACE_NON_UNIFORM_WG_CONFIG;
    std::stringstream kernel_name_ss;
    kernel_name_ss << "-D" << kernel_name << "=" << obfuscated_kernel_name;
    built_options.emplace(kernel_name_ss.str());
    built_options.emplace("-DDATA_TYPE=" + DtToCLDt(DataTypeToEnum<T>::value));
    built_options.emplace("-DCMD_DATA_TYPE=" +
        DtToCLCMDDt(DataTypeToEnum<T>::value));
    MACE_RETURN_IF_ERROR(runtime->BuildKernel("batch_to_space",
                                              obfuscated_kernel_name,
                                              built_options,
                                              &kernel_));

    kwg_size_ =
        static_cast<uint32_t>(runtime->GetKernelMaxWorkGroupSize(kernel_));
  }
  MACE_OUT_OF_RANGE_INIT(kernel_);
  if (!IsVecEqual(input_shape_, batch_tensor->shape())) {
    uint32_t idx = 0;
    MACE_OUT_OF_RANGE_SET_ARGS(kernel_);
    MACE_SET_3D_GWS_ARGS(kernel_, gws);
    kernel_.setArg(idx++, *(batch_tensor->opencl_image()));
    kernel_.setArg(idx++, *(space_tensor->opencl_image()));
    kernel_.setArg(idx++, block_shape[0]);
    kernel_.setArg(idx++, block_shape[1]);
    kernel_.setArg(idx++, paddings[0]);
    kernel_.setArg(idx++, paddings[2]);
    kernel_.setArg(idx++, static_cast<int32_t>(space_tensor->dim(0)));
    kernel_.setArg(idx++, static_cast<int32_t>(space_tensor->dim(1)));
    kernel_.setArg(idx++, static_cast<int32_t>(space_tensor->dim(2)));
    kernel_.setArg(idx++, static_cast<int32_t>(batch_tensor->dim(1)));
    kernel_.setArg(idx++, static_cast<int32_t>(batch_tensor->dim(2)));

    input_shape_ = batch_tensor->shape();
  }

  const std::vector<uint32_t> lws = Default3DLocalWS(runtime, gws, kwg_size_);
  std::string tuning_key =
      Concat("batch_to_space", batch_tensor->dim(0), batch_tensor->dim(1),
             batch_tensor->dim(2), batch_tensor->dim(3));
  MACE_RETURN_IF_ERROR(TuningOrRun3DKernel(runtime, kernel_, tuning_key,
                                           gws, lws, future));

  MACE_OUT_OF_RANGE_VALIDATION;
  return MACE_SUCCESS;
}

}  // namespace image
}  // namespace opencl
}  // namespace kernels
}  // namespace mace

#endif  // MACE_KERNELS_OPENCL_IMAGE_BATCH_TO_SPACE_H_
