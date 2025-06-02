// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "impeller/toolkit/interop/backend/vulkan/surface_vk.h"

#include "impeller/renderer/backend/vulkan/command_buffer_vk.h"
#include "impeller/renderer/backend/vulkan/texture_vk.h"
#include "impeller/toolkit/interop/backend/vulkan/context_vk.h"

namespace impeller::interop {

SurfaceVK::SurfaceVK(Context& context,
                     std::shared_ptr<impeller::Surface> surface)
    : Surface(context, std::move(surface)) {}

SurfaceVK::~SurfaceVK() = default;

bool SurfaceVK::Present() const {
  if (Surface::Present()) {
    return true;
  }

  const auto& rt = surface_->GetRenderTarget();
  if (!rt.HasColorAttachment(0u)) {
    return true;
  }

  const auto color0 = rt.GetColorAttachment(0u);
  auto texture =
      color0.resolve_texture ? color0.resolve_texture : color0.texture;
  const auto& texture_vk = TextureVK::Cast(*texture);

  auto context = reinterpret_cast<ContextVK*>(context_.Get());
  auto command_buffer = context->GetContext()->CreateCommandBuffer();

  auto vk_command_buffer =
      CommandBufferVK::Cast(*command_buffer).GetCommandBuffer();
  {
    BarrierVK barrier;
    barrier.new_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.cmd_buffer = vk_command_buffer;
    barrier.src_access = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.src_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    barrier.dst_access = {};
    barrier.dst_stage = vk::PipelineStageFlagBits::eBottomOfPipe;

    if (!texture_vk.SetLayout(barrier)) {
      return false;
    }

    if (vk_command_buffer.end() != vk::Result::eSuccess) {
      return false;
    }
  }

  return true;
}

}  // namespace impeller::interop
