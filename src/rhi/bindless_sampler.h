#pragma once

#include "core/header.h"
#include "resources/texture_sampler.h"

namespace ocarina {

class Texture;

constexpr uint32_t kBindlessSamplerCount = 4;

[[nodiscard]] OC_RHI_API const TextureSampler& bindless_sampler_config(uint32_t index);
[[nodiscard]] OC_RHI_API uint32_t get_bindless_sampler_index(const TextureSampler& sampler);
[[nodiscard]] OC_RHI_API uint32_t get_bindless_sampler_index(const Texture& texture);

}// namespace ocarina
