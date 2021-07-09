// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/impeller/compiler/reflector.h"

#include <atomic>
#include <optional>
#include <set>
#include <sstream>

#include "flutter/fml/closure.h"
#include "flutter/fml/logging.h"
#include "flutter/impeller/compiler/code_gen_template.h"
#include "flutter/impeller/compiler/utilities.h"
#include "flutter/impeller/geometry/matrix.h"
#include "flutter/impeller/geometry/scalar.h"

namespace impeller {
namespace compiler {

using namespace spirv_cross;

static std::string BaseTypeToString(SPIRType::BaseType type) {
  using Type = SPIRType::BaseType;
  switch (type) {
    case Type::Void:
      return "ShaderType::kVoid";
    case Type::Boolean:
      return "ShaderType::kBoolean";
    case Type::SByte:
      return "ShaderType::kSignedByte";
    case Type::UByte:
      return "ShaderType::kUnsignedByte";
    case Type::Short:
      return "ShaderType::kSignedShort";
    case Type::UShort:
      return "ShaderType::kUnsignedShort";
    case Type::Int:
      return "ShaderType::kSignedInt";
    case Type::UInt:
      return "ShaderType::kUnsignedInt";
    case Type::Int64:
      return "ShaderType::kSignedInt64";
    case Type::UInt64:
      return "ShaderType::kUnsignedInt64";
    case Type::AtomicCounter:
      return "ShaderType::kAtomicCounter";
    case Type::Half:
      return "ShaderType::kHalfFloat";
    case Type::Float:
      return "ShaderType::kFloat";
    case Type::Double:
      return "ShaderType::kDouble";
    case Type::Struct:
      return "ShaderType::kStruct";
    case Type::Image:
      return "ShaderType::kImage";
    case Type::SampledImage:
      return "ShaderType::kSampledImage";
    case Type::Sampler:
      return "ShaderType::kSampler";
    default:
      return "ShaderType::kUnknown";
  }
}

static std::string ExecutionModelToString(spv::ExecutionModel model) {
  switch (model) {
    case spv::ExecutionModel::ExecutionModelVertex:
      return "vertex";
    case spv::ExecutionModel::ExecutionModelFragment:
      return "fragment";
    default:
      return "unsupported";
  }
}

static std::string StringToShaderStage(std::string str) {
  if (str == "vertex") {
    return "ShaderStage::kVertex";
  }

  if (str == "fragment") {
    return "ShaderStage::kFragment";
  }

  return "ShaderStage::kUnknown";
}

Reflector::Reflector(Options options,
                     std::shared_ptr<const ParsedIR> ir,
                     std::shared_ptr<const CompilerMSL> compiler)
    : options_(std::move(options)),
      ir_(std::move(ir)),
      compiler_(std::move(compiler)) {
  if (!ir_ || !compiler_) {
    return;
  }

  if (auto template_arguments = GenerateTemplateArguments();
      template_arguments.has_value()) {
    template_arguments_ =
        std::make_unique<nlohmann::json>(std::move(template_arguments.value()));
  } else {
    return;
  }

  reflection_header_ = GenerateReflectionHeader();
  if (!reflection_header_) {
    return;
  }

  reflection_cc_ = GenerateReflectionCC();
  if (!reflection_cc_) {
    return;
  }

  is_valid_ = true;
}

Reflector::~Reflector() = default;

bool Reflector::IsValid() const {
  return is_valid_;
}

std::shared_ptr<fml::Mapping> Reflector::GetReflectionJSON() const {
  if (!is_valid_) {
    return nullptr;
  }

  auto json_string =
      std::make_shared<std::string>(template_arguments_->dump(2u));

  return std::make_shared<fml::NonOwnedMapping>(
      reinterpret_cast<const uint8_t*>(json_string->data()),
      json_string->size(), [json_string](auto, auto) {});
}

std::shared_ptr<fml::Mapping> Reflector::GetReflectionHeader() const {
  return reflection_header_;
}

std::shared_ptr<fml::Mapping> Reflector::GetReflectionCC() const {
  return reflection_cc_;
}

std::optional<nlohmann::json> Reflector::GenerateTemplateArguments() const {
  nlohmann::json root;

  const auto& entrypoints = compiler_->get_entry_points_and_stages();
  if (entrypoints.size() != 1) {
    FML_LOG(ERROR) << "Incorrect number of entrypoints in the shader. Found "
                   << entrypoints.size() << " but expected 1.";
    return std::nullopt;
  }

  {
    root["entrypoint"] = entrypoints.front().name;
    root["shader_name"] = options_.shader_name;
    root["shader_stage"] =
        ExecutionModelToString(entrypoints.front().execution_model);
    root["header_file_name"] = options_.header_file_name;
  }

  const auto shader_resources = compiler_->get_shader_resources();

  if (auto uniform_buffers = ReflectResources(shader_resources.uniform_buffers);
      uniform_buffers.has_value()) {
    root["uniform_buffers"] = std::move(uniform_buffers.value());
  } else {
    return std::nullopt;
  }

  {
    auto& stage_inputs = root["stage_inputs"] = nlohmann::json::array_t{};
    if (auto stage_inputs_json =
            ReflectResources(shader_resources.stage_inputs);
        stage_inputs_json.has_value()) {
      stage_inputs = std::move(stage_inputs_json.value());
    } else {
      return std::nullopt;
    }
  }

  {
    auto combined_sampled_images =
        ReflectResources(shader_resources.sampled_images);
    auto images = ReflectResources(shader_resources.separate_images);
    auto samplers = ReflectResources(shader_resources.separate_samplers);
    if (!combined_sampled_images.has_value() || !images.has_value() ||
        !samplers.has_value()) {
      return std::nullopt;
    }
    auto& sampled_images = root["sampled_images"] = nlohmann::json::array_t{};
    for (auto value : combined_sampled_images.value()) {
      sampled_images.emplace_back(std::move(value));
    }
    for (auto value : images.value()) {
      sampled_images.emplace_back(std::move(value));
    }
    for (auto value : samplers.value()) {
      sampled_images.emplace_back(std::move(value));
    }
  }

  if (auto stage_outputs = ReflectResources(shader_resources.stage_outputs);
      stage_outputs.has_value()) {
    root["stage_outputs"] = std::move(stage_outputs.value());
  } else {
    return std::nullopt;
  }

  {
    auto& struct_definitions = root["struct_definitions"] =
        nlohmann::json::array_t{};
    if (entrypoints.front().execution_model ==
        spv::ExecutionModel::ExecutionModelVertex) {
      if (auto struc =
              ReflectPerVertexStructDefinition(shader_resources.stage_inputs);
          struc.has_value()) {
        struct_definitions.emplace_back(EmitStructDefinition(struc.value()));
      }
    }

    std::set<spirv_cross::ID> known_structs;
    ir_->for_each_typed_id<SPIRType>([&](uint32_t, const SPIRType& type) {
      if (known_structs.find(type.self) != known_structs.end()) {
        // Iterating over types this way leads to duplicates which may cause
        // duplicate struct definitions.
        return;
      }
      known_structs.insert(type.self);
      if (auto struc = ReflectStructDefinition(type.self); struc.has_value()) {
        struct_definitions.emplace_back(EmitStructDefinition(struc.value()));
      }
    });
  }

  return root;
}

std::shared_ptr<fml::Mapping> Reflector::GenerateReflectionHeader() const {
  return InflateTemplate(kReflectionHeaderTemplate);
}

std::shared_ptr<fml::Mapping> Reflector::GenerateReflectionCC() const {
  return InflateTemplate(kReflectionCCTemplate);
}

std::shared_ptr<fml::Mapping> Reflector::InflateTemplate(
    const std::string_view& tmpl) const {
  inja::Environment env;
  env.set_trim_blocks(true);
  env.set_lstrip_blocks(true);

  env.add_callback("camel_case", 1u, [](inja::Arguments& args) {
    return ConvertToCamelCase(args.at(0u)->get<std::string>());
  });

  env.add_callback("to_shader_stage", 1u, [](inja::Arguments& args) {
    return StringToShaderStage(args.at(0u)->get<std::string>());
  });

  auto inflated_template =
      std::make_shared<std::string>(env.render(tmpl, *template_arguments_));

  return std::make_shared<fml::NonOwnedMapping>(
      reinterpret_cast<const uint8_t*>(inflated_template->data()),
      inflated_template->size(), [inflated_template](auto, auto) {});
}

std::optional<nlohmann::json::object_t> Reflector::ReflectResource(
    const spirv_cross::Resource& resource) const {
  nlohmann::json::object_t result;

  result["name"] = resource.name;
  result["descriptor_set"] = compiler_->get_decoration(
      resource.id, spv::Decoration::DecorationDescriptorSet);
  result["binding"] = compiler_->get_decoration(
      resource.id, spv::Decoration::DecorationBinding);
  result["location"] = compiler_->get_decoration(
      resource.id, spv::Decoration::DecorationLocation);
  result["index"] =
      compiler_->get_decoration(resource.id, spv::Decoration::DecorationIndex);
  result["msl_res_0"] =
      compiler_->get_automatic_msl_resource_binding(resource.id);
  result["msl_res_1"] =
      compiler_->get_automatic_msl_resource_binding_secondary(resource.id);
  result["msl_res_2"] =
      compiler_->get_automatic_msl_resource_binding_tertiary(resource.id);
  result["msl_res_3"] =
      compiler_->get_automatic_msl_resource_binding_quaternary(resource.id);
  auto type = ReflectType(resource.type_id);
  if (!type.has_value()) {
    return std::nullopt;
  }
  result["type"] = std::move(type.value());
  return result;
}

std::optional<nlohmann::json::object_t> Reflector::ReflectType(
    const spirv_cross::TypeID& type_id) const {
  nlohmann::json::object_t result;

  const auto type = compiler_->get_type(type_id);

  result["type_name"] = BaseTypeToString(type.basetype);
  result["bit_width"] = type.width;
  result["vec_size"] = type.vecsize;
  result["columns"] = type.columns;

  return result;
}

std::optional<nlohmann::json::array_t> Reflector::ReflectResources(
    const spirv_cross::SmallVector<spirv_cross::Resource>& resources) const {
  nlohmann::json::array_t result;
  result.reserve(resources.size());
  for (const auto& resource : resources) {
    if (auto reflected = ReflectResource(resource); reflected.has_value()) {
      result.emplace_back(std::move(reflected.value()));
    } else {
      return std::nullopt;
    }
  }
  return result;
}

static std::string TypeNameWithPaddingOfSize(size_t size) {
  std::stringstream stream;
  stream << "Padding<" << size << ">";
  return stream.str();
}

struct KnownType {
  std::string name;
  size_t byte_size = 0;
};

static std::optional<KnownType> ReadKnownScalarType(SPIRType::BaseType type) {
  switch (type) {
    case SPIRType::BaseType::Boolean:
      return KnownType{
          .name = "bool",
          .byte_size = sizeof(bool),
      };
    case SPIRType::BaseType::Float:
      return KnownType{
          .name = "Scalar",
          .byte_size = sizeof(Scalar),
      };
    case SPIRType::BaseType::UInt:
      return KnownType{
          .name = "uint32_t",
          .byte_size = sizeof(uint32_t),
      };
    case SPIRType::BaseType::Int:
      return KnownType{
          .name = "int32_t",
          .byte_size = sizeof(int32_t),
      };
    default:
      break;
  }
  return std::nullopt;
}

std::vector<Reflector::StructMember> Reflector::ReadStructMembers(
    const spirv_cross::TypeID& type_id) const {
  const auto& struct_type = compiler_->get_type(type_id);
  FML_CHECK(struct_type.basetype == SPIRType::BaseType::Struct);

  std::vector<StructMember> result;

  size_t total_byte_length = 0;
  for (size_t i = 0; i < struct_type.member_types.size(); i++) {
    const auto& member = compiler_->get_type(struct_type.member_types[i]);

    // Tightly packed 4x4 Matrix is special cased as we know how to work with
    // those.
    if (member.basetype == SPIRType::BaseType::Float &&  //
        member.width == sizeof(Scalar) * 8 &&            //
        member.columns == 4 &&                           //
        member.vecsize == 4                              //
    ) {
      result.emplace_back(StructMember{
          .type = "Matrix",
          .name = GetMemberNameAtIndex(struct_type, i),
          .offset = total_byte_length,
          .byte_length = sizeof(Matrix),
      });
      total_byte_length += sizeof(Matrix);
      continue;
    }

    // Tightly packed Point
    if (member.basetype == SPIRType::BaseType::Float &&  //
        member.width == sizeof(float) * 8 &&             //
        member.columns == 1 &&                           //
        member.vecsize == 2                              //
    ) {
      result.emplace_back(StructMember{
          .type = "Point",
          .name = GetMemberNameAtIndex(struct_type, i),
          .offset = total_byte_length,
          .byte_length = sizeof(Point),
      });
      total_byte_length += sizeof(Point);
      continue;
    }

    // Tightly packed Vector3
    if (member.basetype == SPIRType::BaseType::Float &&  //
        member.width == sizeof(float) * 8 &&             //
        member.columns == 1 &&                           //
        member.vecsize == 3                              //
    ) {
      result.emplace_back(StructMember{
          .type = "Vector3",
          .name = GetMemberNameAtIndex(struct_type, i),
          .offset = total_byte_length,
          .byte_length = sizeof(Vector3),
      });
      total_byte_length += sizeof(Vector3);
      continue;
    }

    // Tightly packed Vector4
    if (member.basetype == SPIRType::BaseType::Float &&  //
        member.width == sizeof(float) * 8 &&             //
        member.columns == 1 &&                           //
        member.vecsize == 4                              //
    ) {
      result.emplace_back(StructMember{
          .type = "Vector4",
          .name = GetMemberNameAtIndex(struct_type, i),
          .offset = total_byte_length,
          .byte_length = sizeof(Vector4),
      });
      total_byte_length += sizeof(Vector4);
      continue;
    }

    // Other single isolated scalars.
    {
      auto maybe_known_type = ReadKnownScalarType(member.basetype);
      if (maybe_known_type.has_value() &&  //
          member.columns == 1 &&           //
          member.vecsize == 1              //
      ) {
        // Add the type directly.
        result.emplace_back(StructMember{
            .type = maybe_known_type.value().name,
            .name = GetMemberNameAtIndex(struct_type, i),
            .offset = total_byte_length,
            .byte_length = maybe_known_type.value().byte_size,
        });
        total_byte_length += maybe_known_type.value().byte_size;

        // Consider any excess padding.
        const auto padding =
            (member.width / 8u) - maybe_known_type.value().byte_size;
        if (padding != 0) {
          result.emplace_back(StructMember{
              .type = TypeNameWithPaddingOfSize(padding),
              .name = GetMemberNameAtIndex(struct_type, i, "_pad"),
              .offset = total_byte_length,
              .byte_length = padding,
          });
          total_byte_length += padding;
        }
        continue;
      }
    }

    // Catch all for unknown types. Just add the necessary padding to the struct
    // and move on.
    {
      const size_t byte_length =
          (member.width * member.columns * member.vecsize) / 8u;
      result.emplace_back(StructMember{
          .type = TypeNameWithPaddingOfSize(byte_length),
          .name = GetMemberNameAtIndex(struct_type, i),
          .offset = total_byte_length,
          .byte_length = byte_length,
      });
      total_byte_length += byte_length;
    }
  }
  return result;
}

std::optional<Reflector::StructDefinition> Reflector::ReflectStructDefinition(
    const TypeID& type_id) const {
  const auto& type = compiler_->get_type(type_id);
  if (type.basetype != SPIRType::BaseType::Struct) {
    return std::nullopt;
  }

  const auto struct_name = compiler_->get_name(type_id);
  if (struct_name.find("_RESERVED_IDENTIFIER_") != std::string::npos) {
    return std::nullopt;
  }

  size_t total_size = 0u;
  for (const auto& member_type_id : type.member_types) {
    const auto& member_type = compiler_->get_type(member_type_id);
    total_size +=
        (member_type.width * member_type.vecsize * member_type.columns) / 8u;
  }

  StructDefinition struc;
  struc.name = struct_name;
  struc.byte_length = total_size;
  struc.members = ReadStructMembers(type_id);
  return struc;
}

nlohmann::json::object_t Reflector::EmitStructDefinition(
    std::optional<Reflector::StructDefinition> struc) const {
  nlohmann::json::object_t result;
  result["name"] = struc->name;
  result["byte_length"] = struc->byte_length;
  auto& members = result["members"] = nlohmann::json::array_t{};
  for (const auto& struc_member : struc->members) {
    auto& member = members.emplace_back(nlohmann::json::object_t{});
    member["name"] = struc_member.name;
    member["type"] = struc_member.type;
    member["offset"] = struc_member.offset;
    member["byte_length"] = struc_member.byte_length;
  }
  return result;
}

struct VertexType {
  std::string type_name;
  std::string variable_name;
  size_t byte_length = 0u;
};

static VertexType VertexTypeFromInputResource(const CompilerMSL& compiler,
                                              const Resource* resource) {
  VertexType result;
  result.variable_name = resource->name;
  auto type = compiler.get_type(resource->type_id);
  const auto total_size = type.columns * type.vecsize * type.width / 8u;
  result.byte_length = total_size;

  if (type.basetype == SPIRType::BaseType::Float && type.columns == 1u &&
      type.vecsize == 2u && type.width == sizeof(float) * 8u) {
    result.type_name = "Point";
  } else if (type.basetype == SPIRType::BaseType::Float && type.columns == 1u &&
             type.vecsize == 4u && type.width == sizeof(float) * 8u) {
    result.type_name = "Vector4";
  } else if (type.basetype == SPIRType::BaseType::Float && type.columns == 1u &&
             type.vecsize == 3u && type.width == sizeof(float) * 8u) {
    result.type_name = "Vector3";
  } else {
    // Catch all unknown padding.
    result.type_name = TypeNameWithPaddingOfSize(total_size);
  }

  return result;
}

std::optional<Reflector::StructDefinition>
Reflector::ReflectPerVertexStructDefinition(
    const SmallVector<spirv_cross::Resource>& stage_inputs) const {
  // Avoid emitting a zero sized structure. The code gen templates assume a
  // non-zero size.
  if (stage_inputs.empty()) {
    return std::nullopt;
  }

  // Validate locations are contiguous and there are no duplicates.
  std::set<uint32_t> locations;
  for (const auto& input : stage_inputs) {
    auto location = compiler_->get_decoration(
        input.id, spv::Decoration::DecorationLocation);
    if (locations.count(location) != 0) {
      // Duplicate location. Bail.
      return std::nullopt;
    }
    locations.insert(location);
  }

  for (size_t i = 0; i < locations.size(); i++) {
    if (locations.count(i) != 1) {
      // Locations are not contiguous. Bail.
      return std::nullopt;
    }
  }

  auto input_for_location = [&](uint32_t queried_location) -> const Resource* {
    for (const auto& input : stage_inputs) {
      auto location = compiler_->get_decoration(
          input.id, spv::Decoration::DecorationLocation);
      if (location == queried_location) {
        return &input;
      }
    }
    // This really cannot happen with all the validation above.
    return nullptr;
  };

  StructDefinition struc;
  struc.name = "PerVertexData";
  struc.byte_length = 0u;
  for (size_t i = 0; i < locations.size(); i++) {
    auto resource = input_for_location(i);
    if (resource == nullptr) {
      return std::nullopt;
    }
    const auto vertex_type = VertexTypeFromInputResource(*compiler_, resource);

    StructMember member;
    member.name = vertex_type.variable_name;
    member.type = vertex_type.type_name;
    member.byte_length = vertex_type.byte_length;
    member.offset = struc.byte_length;
    struc.byte_length += vertex_type.byte_length;
    struc.members.emplace_back(std::move(member));
  }
  return struc;
}

std::optional<std::string> Reflector::GetMemberNameAtIndexIfExists(
    const spirv_cross::SPIRType& parent_type,
    size_t index) const {
  if (parent_type.type_alias != 0) {
    return GetMemberNameAtIndexIfExists(
        compiler_->get_type(parent_type.type_alias), index);
  }

  if (auto found = ir_->meta.find(parent_type.self); found != ir_->meta.end()) {
    const auto& members = found->second.members;
    if (index < members.size() && !members[index].alias.empty()) {
      return members[index].alias;
    }
  }
  return std::nullopt;
}

std::string Reflector::GetMemberNameAtIndex(
    const spirv_cross::SPIRType& parent_type,
    size_t index,
    std::string suffix) const {
  if (auto name = GetMemberNameAtIndexIfExists(parent_type, index);
      name.has_value()) {
    return name.value();
  }
  static std::atomic_size_t sUnnamedMembersID;
  std::stringstream stream;
  stream << "unnamed_" << sUnnamedMembersID++ << suffix;
  return stream.str();
}

}  // namespace compiler
}  // namespace impeller
