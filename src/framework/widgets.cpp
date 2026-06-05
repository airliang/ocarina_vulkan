//
// Created by Zero on 2024/3/22.
//

#include "widgets.h"
#include "core/logging.h"

namespace ocarina {

bool Widgets::open_file_dialog(std::filesystem::path &path, const FileDialogFilterVec &filters) noexcept {
#if defined(_WIN32)
    return open_file_dialog_win32(path, filters);
#else
    (void)path;
    (void)filters;
    OC_WARNING("open_file_dialog is not implemented on this platform.");
    return false;
#endif
}

bool Widgets::slider_floatN(const std::string &label, float *val, ocarina::uint size, float min, float max) noexcept {
    switch (size) {
        case 1:
            return slider_float(label, val, min, max);
        case 2:
            return slider_float2(label, reinterpret_cast<float2 *>(val), min, max);
        case 3:
            return slider_float3(label, reinterpret_cast<float3 *>(val), min, max);
        case 4:
            return slider_float4(label, reinterpret_cast<float4 *>(val), min, max);
        default:
            OC_ERROR("error");
            break;
    }
    return false;
}

bool Widgets::colorN_edit(const std::string &label, float *val, ocarina::uint size) noexcept {
    switch (size) {
        case 3:
            return color_edit(label, reinterpret_cast<float3 *>(val));
        case 4:
            return color_edit(label, reinterpret_cast<float4 *>(val));
        default:
            OC_ERROR("error");
            return false;
    }
}

bool Widgets::input_floatN(const std::string &label, float *val, ocarina::uint size) noexcept {
    switch (size) {
        case 1:
            return input_float(label, val);
        case 2:
            return input_float2(label, reinterpret_cast<float2 *>(val));
        case 3:
            return input_float3(label, reinterpret_cast<float3 *>(val));
        case 4:
            return input_float4(label, reinterpret_cast<float4 *>(val));
        default:
            OC_ERROR("error");
            break;
    }
    return false;
}

bool Widgets::drag_floatN(const std::string &label, float *val, ocarina::uint size,
                          float speed, float min, float max, const char *fmt) noexcept {
    switch (size) {
        case 1:
            return drag_float(label, val, speed, min, max, fmt);
        case 2:
            return drag_float2(label, reinterpret_cast<float2 *>(val), speed, min, max, fmt);
        case 3:
            return drag_float3(label, reinterpret_cast<float3 *>(val), speed, min, max, fmt);
        case 4:
            return drag_float4(label, reinterpret_cast<float4 *>(val), speed, min, max, fmt);
        default:
            OC_ERROR("error");
            break;
    }
    return false;
}

}// namespace ocarina
