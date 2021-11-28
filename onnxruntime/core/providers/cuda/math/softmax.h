// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "gsl/gsl"
#include "core/providers/cuda/cuda_kernel.h"
#include "core/framework/random_generator.h"

namespace onnxruntime {
namespace cuda {

template <typename T, bool is_log_softmax>
Status SoftMaxComputeHelper(
    cudaStream_t stream,
    const T* input,
    const TensorShape& shape,
    T* Y,
    int64_t axis);

template <typename input_t, typename output_t, typename acc_t, bool is_log_softmax, bool mask_input, bool dropout_output>
void dispatch_warpwise_softmax_forward(cudaStream_t stream, output_t* dst, const input_t* src, int softmax_elements, int softmax_elements_stride, int batch_count,
  PhiloxGenerator* generator=nullptr, const float dropout_ratio=1.0f, input_t* dropout_result=nullptr, void* dropout_mask=nullptr);

template <typename input_t, typename output_t, typename acc_t, bool is_log_softmax>
void dispatch_blockwise_softmax_forward(cudaStream_t stream, output_t* output, const input_t* input, int softmax_elements, int softmax_elements_stride, int batch_count);

template <typename T>
class Softmax final : public CudaKernel {
 public:
  Softmax(const OpKernelInfo& info) : CudaKernel{info} {
    const auto& node = info.node();
    opset_ = node.SinceVersion();

    int64_t axis;
    Status status = info.GetAttr<int64_t>("axis", &axis);

    if (status.IsOK()) {
      axis_ = gsl::narrow_cast<int>(axis);
    } else {
      if (opset_ < 13) {
        axis_ = 1;  // opset-12 and below, the default axis value is 1
      } else {
        axis_ = -1;  // opset-13, the default axis value is -1
      }
    }

    log_softmax_ = info.GetKernelDef().OpName() == "LogSoftmax";

    // We need to cast away the const as PerThreadCublasHandle() is currently a non-const method
    // TODO: Clean up the CUDAExecutionProvider interface to avoid this
    cuda_ep_ = const_cast<CUDAExecutionProvider*>(
        static_cast<const CUDAExecutionProvider*>(info.GetExecutionProvider()));
  }

  Status ComputeInternal(OpKernelContext* context) const override;

 private:
  int64_t axis_;
  bool log_softmax_;
  int opset_;

  // We need to access to the CUDA EP instance to get the cublas handle to use
  // for transposing(if applicable)
  CUDAExecutionProvider* cuda_ep_;
};


template <typename T, typename T1, typename T2>
class AdditiveMaskSoftmaxDropout final : public CudaKernel {
 public:
  AdditiveMaskSoftmaxDropout(const OpKernelInfo& info) : CudaKernel{info} {
    // const auto& node = info.node();
    // int64_t axis;
    // Status status = info.GetAttr<int64_t>("axis", &axis);

    log_softmax_ = info.GetKernelDef().OpName() == "LogSoftmax";

    int64_t seed = 0;
    if (info.GetAttr<int64_t>("seed", &seed).IsOK()) {
      generator_ = std::make_unique<PhiloxGenerator>(static_cast<uint64_t>(seed));
    }
  }

  Status ComputeInternal(OpKernelContext* context) const override;

 private:
  int64_t axis_;
  bool log_softmax_;

  mutable std::unique_ptr<PhiloxGenerator> generator_;
  static constexpr float default_ratio_ = 0.5f;
};

}  // namespace cuda
}  // namespace onnxruntime
