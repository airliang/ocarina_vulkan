//
// Created by Zero on 16/05/2022.
//

#include "printer.h"

namespace ocarina {

void Printer::output_log(const OutputFunc &func) noexcept {
    uint length = std::min(
        static_cast<uint>(buffer_.host_buffer().size() - 1u),
        buffer_.back());
    bool truncated = buffer_.host_buffer().back() > length;
    uint offset = 0u;
    while (offset < length) {
        const uint *data = buffer_.host_buffer().data() + offset;
        Item &item = items_[data[0]];
        offset += item.size + 1;
        if (offset > length) {
            truncated = true;
        } else {
            item.func(data + 1, func);
        }
    }
    if (truncated) [[unlikely]] {
        OC_WARNING("Kernel log truncated.");
    }
}

CommandList Printer::retrieve(const OutputFunc &func) noexcept {
    if (!enabled_) {
        return {};
    }
    CommandList ret;
    ret << [&]() {
        buffer_.download_immediately();
        output_log(func);
        buffer_.resize(buffer_.capacity());
    };
    return ret;
}

void Printer::retrieve_immediately(const OutputFunc &func) noexcept {
    if (!enabled_) {
        return;
    }
    buffer_.download_immediately();
    output_log(func);
    reset();
}
}// namespace ocarina