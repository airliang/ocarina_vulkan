#include "bindless_sampler.h"
#include "resources/texture.h"

namespace ocarina {

const TextureSampler& bindless_sampler_config(uint32_t index) {
    static const TextureSampler configs[] = {
        {TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::REPEAT},
        {TextureSampler::Filter::LINEAR_LINEAR, TextureSampler::Address::CLAMP},
        {TextureSampler::Filter::POINT, TextureSampler::Address::REPEAT},
        {TextureSampler::Filter::POINT, TextureSampler::Address::CLAMP},
    };
    OC_ASSERT(index < kBindlessSamplerCount);
    return configs[index];
}

uint32_t get_bindless_sampler_index(const TextureSampler& sampler) {
    for (uint32_t i = 0; i < kBindlessSamplerCount; ++i) {
        const TextureSampler& config = bindless_sampler_config(i);
        if (sampler.filter() == config.filter() && sampler.u_address() == config.u_address()) {
            return i;
        }
    }
    OC_ASSERT(false && "TextureSampler is not supported by the bindless sampler table");
    return 0;
}

uint32_t get_bindless_sampler_index(const Texture& texture) {
    const TextureSampler* sampler = texture.get_sampler_pointer();
    OC_ASSERT(sampler != nullptr);
    return get_bindless_sampler_index(*sampler);
}

}// namespace ocarina
