/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/delegates/gpu/cl/kernels/elementwise.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "tensorflow/lite/delegates/gpu/cl/kernels/util.h"
#include "tensorflow/lite/delegates/gpu/cl/storage_type_util.h"

namespace tflite {
namespace gpu {
namespace cl {
namespace {
std::string GetOneInputCode(const OperationType& op_type,
                            CalculationsPrecision precision,
                            const std::string& input0) {
  std::string result;
  switch (op_type) {
    case OperationType::ABS:
      result = "$0 = fabs($0);\n";
      break;
    case OperationType::COS:
      result = "$0 = cos($0);\n";
      break;
    case OperationType::EXP:
      result = "$0 = exp($0);\n";
      break;
    case OperationType::HARD_SWISH:
      result =
          "$0 *= clamp($0 * (FLT)(0.16666667f) + (FLT)(0.5f), (FLT4)(0.0f), "
          "(FLT4)(1.0f));\n";
      break;
    case OperationType::LOG:
      result = "$0 = log($0);\n";
      break;
    case OperationType::RSQRT:
      result = "$0 = (FLT4)(1.0f) / sqrt($0);\n";
      break;
    case OperationType::SIGMOID:
      if (precision != CalculationsPrecision::F32) {
        result =
            "$0.x = convert_half(native_recip(1.0f + "
            "native_exp(convert_float(-$0.x))));\n";
        result +=
            "$0.y = convert_half(native_recip(1.0f + "
            "native_exp(convert_float(-$0.y))));\n";
        result +=
            "$0.z = convert_half(native_recip(1.0f + "
            "native_exp(convert_float(-$0.z))));\n";
        result +=
            "$0.w = convert_half(native_recip(1.0f + "
            "native_exp(convert_float(-$0.w))));\n";
      } else {
        result = "$0 = (FLT4)(1.0f) / ((FLT4)(1.0f) + exp(-($0)));\n";
      }
      break;
    case OperationType::SIN:
      result = "$0 = sin($0);\n";
      break;
    case OperationType::SQRT:
      result = "$0 = sqrt($0);\n";
      break;
    case OperationType::SQUARE:
      result = "$0 *= $0;\n";
      break;
    case OperationType::TANH:
      result = "$0 = tanh($0);\n";
      break;
    default:
      return "Unknown operation type;\n";
  }
  return absl::Substitute(result, input0);
}

std::string GetTwoInputCode(const OperationType& op_type,
                            const std::string& input0,
                            const std::string& input1) {
  std::string result;
  switch (op_type) {
    case OperationType::ADD:
      result += "$0 += $1;\n";
      break;
    case OperationType::DIV:
      result += "$0 /= $1;\n";
      break;
    case OperationType::MAXIMUM:
      result += "$0 = max($0, $1);\n";
      break;
    case OperationType::MINIMUM:
      result += "$0 = min($0, $1);\n";
      break;
    case OperationType::MUL:
      result += "$0 *= $1;\n";
      break;
    case OperationType::POW:
      result += "$0 = pow($0, $1);\n";
      break;
    case OperationType::SQUARED_DIFF:
      result += "$0 -= $1;\n";
      result += "$0 *= $0;\n";
      break;
    case OperationType::SUB:
      result += "$0 -= $1;\n";
      break;
    default:
      return "Unknown operation type;\n";
  }
  return absl::Substitute(result, input0, input1);
}
}  // namespace

ElementwiseOneInput::ElementwiseOneInput(const OperationDef& definition,
                                         const OperationType& op_type)
    : ElementwiseOperation(definition), op_type_(op_type) {
  code_ = GetOneInputCode(op_type, definition.precision, "in_out_value");
}

ElementwiseOneInput::ElementwiseOneInput(ElementwiseOneInput&& operation)
    : ElementwiseOperation(std::move(operation)),
      op_type_(operation.op_type_) {}

ElementwiseOneInput& ElementwiseOneInput::operator=(
    ElementwiseOneInput&& operation) {
  if (this != &operation) {
    std::swap(op_type_, operation.op_type_);
    ElementwiseOperation::operator=(std::move(operation));
  }
  return *this;
}

ElementwiseOneInput CreateElementwiseOneInput(const OperationDef& definition,
                                              const OperationType& op_type) {
  ElementwiseOneInput operation(definition, op_type);
  return operation;
}

ElementwiseOneRuntimeOneScalar::ElementwiseOneRuntimeOneScalar(
    const OperationDef& definition, const OperationType& op_type,
    float scalar_parameter, CalculationsPrecision scalar_precision)
    : ElementwiseOperation(definition), op_type_(op_type) {
  if (definition.precision == CalculationsPrecision::F32) {
    args_.AddFloat("scalar", scalar_parameter);
  } else {
    args_.AddHalf("scalar", half(scalar_parameter));
  }
  code_ = GetTwoInputCode(op_type, "in_out_value", "args.scalar");
}

ElementwiseOneRuntimeOneScalar::ElementwiseOneRuntimeOneScalar(
    ElementwiseOneRuntimeOneScalar&& operation)
    : ElementwiseOperation(std::move(operation)),
      link_index_(operation.link_index_),
      op_type_(operation.op_type_) {}

ElementwiseOneRuntimeOneScalar& ElementwiseOneRuntimeOneScalar::operator=(
    ElementwiseOneRuntimeOneScalar&& operation) {
  if (this != &operation) {
    link_index_ = operation.link_index_;
    op_type_ = operation.op_type_;
    ElementwiseOperation::operator=(std::move(operation));
  }
  return *this;
}

ElementwiseOneRuntimeOneScalar CreateElementwiseOneRuntimeOneScalar(
    const CreationContext& creation_context, const OperationDef& definition,
    const OperationType& op_type, float scalar_parameter) {
  const auto scalar_precision = creation_context.device->IsPowerVR()
                                    ? CalculationsPrecision::F32
                                    : definition.precision;
  ElementwiseOneRuntimeOneScalar operation(definition, op_type,
                                           scalar_parameter, scalar_precision);
  return operation;
}

ElementwiseTwoInput::ElementwiseTwoInput(const OperationDef& definition,
                                         const OperationType& op_type,
                                         const BroadcastSettings& broadcast)
    : ElementwiseOperation(definition),
      op_type_(op_type),
      broadcast_(broadcast) {
  auto src_desc =
      absl::make_unique<TensorDescriptor>(definition.src_tensors[1]);
  if (definition.IsBatchSupported()) {
    src_desc->SetStateVar("BatchedWidth", "true");
  }
  args_.AddObjectRef("second_tensor", AccessType::READ, std::move(src_desc));
  const std::string x_coord = broadcast.width ? "0" : "X_COORD";
  const std::string y_coord = broadcast.height ? "0" : "Y_COORD";
  const std::string s_coord = broadcast.channels ? "0" : "S_COORD";
  code_ = absl::StrCat("FLT4 second_val = args.second_tensor.Read(", x_coord,
                       ", ", y_coord, ", ", s_coord, ");\n");
  if (broadcast.channels) {
    code_ += "  second_val.y = second_val.x;\n";
    code_ += "  second_val.z = second_val.x;\n";
    code_ += "  second_val.w = second_val.x;\n";
  }
  code_ += GetTwoInputCode(op_type, "in_out_value", "second_val");
}

ElementwiseTwoInput::ElementwiseTwoInput(const OperationDef& definition,
                                         const OperationType& op_type,
                                         const BroadcastSettings& broadcast,
                                         Tensor&& constant_tensor)
    : ElementwiseOperation(definition),
      op_type_(op_type),
      broadcast_(broadcast) {
  auto descriptor = constant_tensor.GetDescriptor();
  args_.AddObject("second_tensor", AccessType::READ,
                  absl::make_unique<Tensor>(std::move(constant_tensor)),
                  absl::make_unique<TensorDescriptor>(descriptor));
  const std::string x_coord = broadcast.width ? "0" : "X_COORD";
  const std::string y_coord = broadcast.height ? "0" : "Y_COORD";
  const std::string s_coord = broadcast.channels ? "0" : "S_COORD";
  code_ = absl::StrCat("FLT4 second_val = args.second_tensor.Read(", x_coord,
                       ", ", y_coord, ", ", s_coord, ");\n");
  if (broadcast.channels) {
    code_ += "  second_val.y = second_val.x;\n";
    code_ += "  second_val.z = second_val.x;\n";
    code_ += "  second_val.w = second_val.x;\n";
  }
  code_ += GetTwoInputCode(op_type, "in_out_value", "second_val");
}

ElementwiseTwoInput::ElementwiseTwoInput(ElementwiseTwoInput&& operation)
    : ElementwiseOperation(std::move(operation)),
      link_index_(operation.link_index_),
      op_type_(operation.op_type_),
      broadcast_(operation.broadcast_) {}

ElementwiseTwoInput& ElementwiseTwoInput::operator=(
    ElementwiseTwoInput&& operation) {
  if (this != &operation) {
    link_index_ = operation.link_index_;
    op_type_ = operation.op_type_;
    broadcast_ = operation.broadcast_;
    ElementwiseOperation::operator=(std::move(operation));
  }
  return *this;
}

absl::Status ElementwiseTwoInput::SetArgs(const std::string& unique_postfix,
                                          Arguments* args) {
  std::string tensor_name = absl::StrCat("second_tensor", unique_postfix);
  if (src_.size() == 2) {
    RETURN_IF_ERROR(args->SetObjectRef(tensor_name, src_[1]));
  }
  return absl::OkStatus();
}

absl::Status CreateElementwiseTwoInput(
    const CreationContext& creation_context, const OperationDef& definition,
    const OperationType& op_type,
    const tflite::gpu::Tensor<Linear, DataType::FLOAT32>& constant_tensor,
    ElementwiseTwoInput* result) {
  const BHWC shape = BHWC(1, 1, 1, constant_tensor.shape.v);
  TensorStorageType storage_type =
      SelectBestStorageType(*creation_context.context, *creation_context.device,
                            shape, definition.GetPrimaryStorageType(),
                            definition.GetDataType(), Layout::HWC);
  TensorDescriptor desc{definition.GetDataType(), storage_type, Layout::HWC};
  Tensor gpu_tensor;
  RETURN_IF_ERROR(CreateTensor(*creation_context.context,
                               *creation_context.device, shape, desc,
                               &gpu_tensor));
  RETURN_IF_ERROR(
      gpu_tensor.WriteData(creation_context.queue, constant_tensor));
  BroadcastSettings broadcast;
  broadcast.width = true;
  broadcast.height = true;
  broadcast.channels = shape.c == 1;
  *result = ElementwiseTwoInput(definition, op_type, broadcast,
                                std::move(gpu_tensor));
  return absl::OkStatus();
}

absl::Status CreateElementwiseTwoInput(
    const CreationContext& creation_context, const OperationDef& definition,
    const OperationType& op_type,
    const tflite::gpu::Tensor<HWC, DataType::FLOAT32>& constant_tensor,
    ElementwiseTwoInput* result) {
  const BHWC shape = BHWC(1, constant_tensor.shape.h, constant_tensor.shape.w,
                          constant_tensor.shape.c);
  TensorStorageType storage_type =
      SelectBestStorageType(*creation_context.context, *creation_context.device,
                            shape, definition.GetPrimaryStorageType(),
                            definition.GetDataType(), Layout::HWC);
  TensorDescriptor desc{definition.GetDataType(), storage_type, Layout::HWC};
  Tensor gpu_tensor;
  RETURN_IF_ERROR(CreateTensor(*creation_context.context,
                               *creation_context.device, shape, desc,
                               &gpu_tensor));
  RETURN_IF_ERROR(
      gpu_tensor.WriteData(creation_context.queue, constant_tensor));
  BroadcastSettings broadcast;
  broadcast.width = shape.w == 1;
  broadcast.height = shape.h == 1;
  broadcast.channels = shape.c == 1;
  *result = ElementwiseTwoInput(definition, op_type, broadcast,
                                std::move(gpu_tensor));
  return absl::OkStatus();
}

ElementwiseTwoInput CreateElementwiseTwoInput(const OperationDef& definition,
                                              const OperationType& op_type,
                                              const BHWC& shape) {
  BroadcastSettings broadcast;
  broadcast.width = shape.w == 1;
  broadcast.height = shape.h == 1;
  broadcast.channels = shape.c == 1;
  ElementwiseTwoInput operation(definition, op_type, broadcast);
  return operation;
}

ElementwiseTwoInput CreateElementwiseTwoInput(const OperationDef& definition,
                                              const OperationType& op_type) {
  BroadcastSettings broadcast;
  broadcast.width = false;
  broadcast.height = false;
  broadcast.channels = false;
  ElementwiseTwoInput operation(definition, op_type, broadcast);
  return operation;
}

}  // namespace cl
}  // namespace gpu
}  // namespace tflite
