/*!
 *  Copyright (c) 2016 by Contributors
 * \file c_runtime_api.cc
 * \brief Device specific implementations
 */
#include <tvm/c_runtime_api.h>
#include <algorithm>
#include "./runtime_base.h"
#include "./device_api.h"

namespace tvm {
namespace runtime {

inline TVMArray* TVMArrayCreate_() {
  TVMArray* arr = new TVMArray();
  arr->shape = nullptr;
  arr->strides = nullptr;
  arr->ndim = 0;
  arr->data = nullptr;
  return arr;
}

inline void TVMArrayFree_(TVMArray* arr) {
  if (arr != nullptr) {
    // ok to delete nullptr
    delete[] arr->shape;
    delete[] arr->strides;
    if (arr->data != nullptr) {
      TVM_DEVICE_SWITCH(arr->ctx, {
          FreeDataSpace<xpu>(arr->ctx, arr->data);
        });
    }
  }
  delete arr;
}

inline void VerifyType(TVMDataType dtype) {
  CHECK_GE(dtype.lanes, 1U);
  if (dtype.type_code == kFloat) {
    CHECK_EQ(dtype.bits % 32U, 0U);
  } else {
    CHECK_EQ(dtype.bits % 8U, 0U);
  }
  CHECK_EQ(dtype.bits & (dtype.bits - 1), 0);
}

inline size_t GetDataSize(TVMArray* arr) {
  size_t size = 1;
  for (tvm_index_t i = 0; i < arr->ndim; ++i) {
    size *= arr->shape[i];
  }
  size *= (arr->dtype.bits / 8) * arr->dtype.lanes;
  return size;
}

inline size_t GetDataAlignment(TVMArray* arr) {
  size_t align = (arr->dtype.bits / 8) * arr->dtype.lanes;
  if (align < 8) return 8;
  return align;
}

}  // namespace runtime
}  // namespace tvm

using namespace tvm::runtime;

int TVMContextEnabled(TVMContext ctx,
                      int* out_enabled) {
  API_BEGIN();
  if (ctx.dev_mask == kGPU && TVM_CUDA_RUNTIME == 0) {
    *out_enabled = 0;
  } else if (ctx.dev_mask == kOpenCL && TVM_OPENCL_RUNTIME == 0) {
    *out_enabled = 0;
  } else {
    TVM_DEVICE_SWITCH(ctx, {
        *out_enabled = CheckEnabled<xpu>(ctx);
      });
  }
  API_END();
}

int TVMArrayAlloc(const tvm_index_t* shape,
                  tvm_index_t ndim,
                  TVMDataType dtype,
                  TVMContext ctx,
                  TVMArrayHandle* out) {
  TVMArray* arr = nullptr;
  API_BEGIN();
  // shape
  arr = TVMArrayCreate_();
  // ndim
  arr->ndim = ndim;
  // dtype
  VerifyType(dtype);
  arr->dtype = dtype;
  tvm_index_t* shape_copy = new tvm_index_t[ndim];
  std::copy(shape, shape + ndim, shape_copy);
  arr->shape = shape_copy;
  // ctx
  arr->ctx = ctx;
  size_t size = GetDataSize(arr);
  size_t alignment = GetDataAlignment(arr);
  // ctx data pointer
  TVM_DEVICE_SWITCH(ctx, {
      arr->data = AllocDataSpace<xpu>(ctx, size, alignment);
    });
  *out = arr;
  API_END_HANDLE_ERROR(TVMArrayFree_(arr));
}

int TVMArrayFree(TVMArrayHandle handle) {
  API_BEGIN();
  TVMArray* arr = handle;
  TVMArrayFree_(arr);
  API_END();
}

int TVMArrayCopyFromTo(TVMArrayHandle from,
                       TVMArrayHandle to,
                       TVMStreamHandle stream) {
  API_BEGIN();
  size_t from_size = GetDataSize(from);
  size_t to_size = GetDataSize(to);
  CHECK_EQ(from_size, to_size)
      << "TVMArrayCopyFromTo: The size must exactly match";
  TVMContext ctx = from->ctx;
  if (ctx.dev_mask == kCPU) {
    ctx = to->ctx;
  } else {
    CHECK(to->ctx.dev_mask == kCPU ||
          to->ctx.dev_mask == from->ctx.dev_mask)
        << "Can not copy across different ctx types directly";
  }

  TVM_DEVICE_SWITCH(ctx, {
      CopyDataFromTo<xpu>(from->data, to->data,
                          from_size,
                          from->ctx,
                          to->ctx,
                          stream);
    });
  API_END();
}

int TVMSynchronize(TVMContext ctx, TVMStreamHandle stream) {
  API_BEGIN();
  TVM_DEVICE_SWITCH(ctx, {
      StreamSync<xpu>(ctx, stream);
    });
  API_END();
}