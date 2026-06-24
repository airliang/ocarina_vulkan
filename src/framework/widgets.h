//
// Created by Zero on 2024/3/16.
//

#pragma once

#include <utility>
#include "core/header.h"
#include "core/stl.h"
#include "math/basic_types.h"
#include "core/util.h"
#include "core/image.h"

namespace ocarina {

struct FileDialogFilter {
    explicit FileDialogFilter(std::string ext_, std::string desc_ = {})
        : desc(std::move(desc_)), ext(std::move(ext_)) {}
    std::string desc;
    std::string ext;
};

using FileDialogFilterVec = std::vector<FileDialogFilter>;

#if defined(_WIN32)
bool open_file_dialog_win32(std::filesystem::path &path, const FileDialogFilterVec &filters) noexcept;
#endif

enum WindowFlag {
    None = 0,
    MenuBar = 1 << 10
};

class SDLWindow;
struct FrameInfoContext;

class OC_FRAMEWORK_API Widgets {
private:
    SDLWindow *window_{nullptr};
    FrameInfoContext *frame_info_context_{nullptr};

public:
    explicit Widgets(SDLWindow *window = nullptr) : window_(window) {}

    virtual void push_item_width(int width) noexcept = 0;
    virtual void pop_item_width() noexcept = 0;

    virtual void begin_tool_tip() noexcept = 0;
    virtual void end_tool_tip() noexcept = 0;

    virtual void image(uint tex_handle, uint2 size, float2 uv0, float2 uv1) noexcept = 0;
    virtual void image(const Image &image) noexcept = 0;
    virtual void image(const ImageView &image_view) noexcept = 0;
    void adaptive_image(uint tex_handle, uint2 res, float2 uv0 = make_float2(0),
                        float2 uv1 = make_float2(1.f)) {
        float ratio = res.x * 1.f / res.y;
        uint2 size = make_uint2(node_size().x, node_size().x / ratio);
        image(tex_handle, min(size, res), uv0, uv1);
    }

    virtual void begin_disabled() noexcept = 0;
    virtual void end_disabled() noexcept = 0;

    template<typename Func>
    void set_enabled(bool enabled, Func func) noexcept {
        if (enabled) {
            func();
        } else {
            begin_disabled();
            func();
            end_disabled();
        }
    }

    static bool open_file_dialog(std::filesystem::path &path, const FileDialogFilterVec &filters = {}) noexcept;

    virtual uint2 node_size() noexcept = 0;

    void image(uint tex_handle, uint2 size) noexcept {
        image(tex_handle, size, make_float2(0), make_float2(1));
    }

    OC_MAKE_MEMBER_GETTER(window, )

    void set_frame_info_context(FrameInfoContext* context) noexcept { frame_info_context_ = context; }
    [[nodiscard]] FrameInfoContext* frame_info_context() const noexcept { return frame_info_context_; }

    template<typename Func>
    void use_tool_tip(Func &&func) noexcept {
        begin_tool_tip();
        func();
        end_tool_tip();
    }

    template<typename Func>
    void use_item_width(int width, Func &&func) noexcept {
        push_item_width(width);
        func();
        pop_item_width();
    }

    virtual bool push_window(const string &label) noexcept = 0;
    virtual bool push_window(const string &label, WindowFlag flag) noexcept = 0;
    virtual void pop_window() noexcept = 0;

    template<typename Func>
    bool use_window(const string &label, WindowFlag flag, Func &&func) noexcept {
        bool show = push_window(label, flag);
        if (show) {
            func();
        }
        pop_window();
        return show;
    }

    template<typename Func>
    bool use_window(const string &label, Func &&func) noexcept {
        return use_window(label, WindowFlag::None, OC_FORWARD(func));
    }

    virtual bool tree_node(const string &label) noexcept = 0;
    virtual void tree_pop() noexcept = 0;

    virtual void push_id(char *str) noexcept = 0;
    virtual void pop_id() noexcept = 0;

    template<typename T, typename Func>
    bool use_tree(T &&label, Func &&func) noexcept {
        bool show = tree_node(OC_FORWARD(label));
        if (show) {
            func();
            tree_pop();
        }
        return show;
    }

    virtual bool folding_header(const string &label) noexcept = 0;

    template<typename T, typename Func>
    bool use_folding_header(T &&label, Func &&func) noexcept {
        bool open = folding_header(OC_FORWARD(label));
        if (open) {
            func();
        }
        return open;
    }

    virtual bool begin_main_menu_bar() noexcept = 0;
    virtual void end_main_menu_bar() noexcept = 0;

    virtual bool begin_menu_bar() noexcept = 0;
    virtual bool begin_menu(const string &label) noexcept = 0;
    virtual bool menu_item(const string &label) noexcept = 0;
    virtual void end_menu() noexcept = 0;
    virtual void end_menu_bar() noexcept = 0;

    template<typename Func>
    bool use_main_menu_bar(Func &&func) noexcept {
        bool ret = begin_main_menu_bar();
        if (ret) {
            func();
            end_main_menu_bar();
        }
        return ret;
    }

    template<typename Func>
    bool use_menu_bar(Func &&func) noexcept {
        bool ret = begin_menu_bar();
        if (ret) {
            func();
            end_menu_bar();
        }
        return ret;
    }

    template<typename Func>
    bool use_menu(const string &label, Func &&func) noexcept {
        bool ret = begin_menu(label.c_str());
        if (ret) {
            func();
            end_menu();
        }
        return ret;
    }

    virtual bool radio_button(const string &label, bool active) noexcept = 0;

    template<typename Func>
    void use_radio_button(const string &label, Func &&func) noexcept {
        push_window(label);
        func();
        pop_window();
    }
    virtual bool input_text(const string &label, char *buf, size_t buf_size) noexcept = 0;
    virtual void text(const char *format, ...) noexcept = 0;
    void text(const string &str) noexcept {
        text(str.c_str());
    }
    virtual void text_wrapped(const char *format, ...) noexcept = 0;
    virtual void progress_bar(float fraction, uint2 size = make_uint2(220, 0)) noexcept = 0;
    virtual bool push_centered_window(const string &label, float bg_alpha = 0.9f) noexcept = 0;
    virtual bool check_box(const string &label, bool *val) noexcept = 0;

    virtual bool slider_float(const string &label, float *val, float min, float max) noexcept = 0;
    virtual bool slider_float2(const string &label, float2 *val, float min, float max) noexcept = 0;
    virtual bool slider_float3(const string &label, float3 *val, float min, float max) noexcept = 0;
    virtual bool slider_float4(const string &label, float4 *val, float min, float max) noexcept = 0;

    bool slider_floatN(const string &label, float *val, uint size, float min, float max) noexcept;

    virtual bool slider_int(const string &label, int *val, int min, int max) noexcept = 0;
    virtual bool slider_int2(const string &label, int2 *val, int min, int max) noexcept = 0;
    virtual bool slider_int3(const string &label, int3 *val, int min, int max) noexcept = 0;
    virtual bool slider_int4(const string &label, int4 *val, int min, int max) noexcept = 0;

    virtual bool slider_uint(const string &label, uint *val, uint min, uint max) noexcept = 0;
    virtual bool slider_uint2(const string &label, uint2 *val, uint min, uint max) noexcept = 0;
    virtual bool slider_uint3(const string &label, uint3 *val, uint min, uint max) noexcept = 0;
    virtual bool slider_uint4(const string &label, uint4 *val, uint min, uint max) noexcept = 0;

    virtual bool color_edit(const string &label, float3 *val) noexcept = 0;
    virtual bool color_edit(const string &label, float4 *val) noexcept = 0;

    bool colorN_edit(const string &label, float *val, uint size) noexcept;

    virtual bool button(const string &label, uint2 size) noexcept = 0;
    virtual bool button(const string &label) noexcept = 0;

    template<typename Func>
    bool button_click(const string &label, Func &&func) noexcept {
        bool ret = button(label.c_str());
        if (ret) {
            func();
        }
        return ret;
    }

    virtual void same_line() noexcept = 0;
    virtual void new_line() noexcept = 0;

    virtual bool input_int(const string &label, int *val) noexcept = 0;
    virtual bool input_int(const string &label, int *val, int step, int step_fast) noexcept = 0;
    template<typename... Args>
    bool input_int_limit(const string &label, int *val, int min, int max, Args &&...args) noexcept {
        int old_value = *val;
        bool dirty = input_int(label, val, OC_FORWARD(args)...);
        *val = ocarina::clamp(*val, min, max);
        if (*val == old_value) {
            dirty = false;
        }
        return dirty;
    }
    virtual bool input_int2(const string &label, int2 *val) noexcept = 0;
    virtual bool input_int3(const string &label, int3 *val) noexcept = 0;
    virtual bool input_int4(const string &label, int4 *val) noexcept = 0;

    virtual bool input_uint(const string &label, uint *val) noexcept = 0;
    virtual bool input_uint(const string &label, uint *val, uint step, uint step_fast) noexcept = 0;
    template<typename... Args>
    bool input_uint_limit(const string &label, uint *val, uint min, uint max, Args &&...args) noexcept {
        uint old_value = *val;
        bool dirty = input_uint(label, val, OC_FORWARD(args)...);
        *val = ocarina::clamp(*val, min, max);
        if (*val == old_value) {
            dirty = false;
        }
        return dirty;
    }
    virtual bool input_uint2(const string &label, uint2 *val) noexcept = 0;
    virtual bool input_uint3(const string &label, uint3 *val) noexcept = 0;
    virtual bool input_uint4(const string &label, uint4 *val) noexcept = 0;

    virtual bool input_float(const string &label, float *val) noexcept = 0;
    virtual bool input_float(const string &label, float *val, float step, float step_fast) noexcept = 0;
    template<typename... Args>
    bool input_float_limit(const string &label, float *val, float min, float max, Args &&...args) noexcept {
        float old_value = *val;
        bool dirty = input_float(label, val, OC_FORWARD(args)...);
        *val = ocarina::clamp(*val, min, max);
        if (*val == old_value) {
            dirty = false;
        }
        return dirty;
    }
    virtual bool input_float2(const string &label, float2 *val) noexcept = 0;
    virtual bool input_float3(const string &label, float3 *val) noexcept = 0;
    virtual bool input_float4(const string &label, float4 *val) noexcept = 0;

    bool input_floatN(const string &label, float *val, uint size) noexcept;

    virtual bool drag_int(const string &label, int *val, float speed, int min = neg_infinity_v<int>,
                          int max = pos_infinity_v<int>) noexcept = 0;
    virtual bool drag_int2(const string &label, int2 *val, float speed, int min = neg_infinity_v<int>,
                           int max = pos_infinity_v<int>) noexcept = 0;
    virtual bool drag_int3(const string &label, int3 *val, float speed, int min = neg_infinity_v<int>,
                           int max = pos_infinity_v<int>) noexcept = 0;
    virtual bool drag_int4(const string &label, int4 *val, float speed, int min = neg_infinity_v<int>,
                           int max = pos_infinity_v<int>) noexcept = 0;

    virtual bool drag_uint(const string &label, uint *val, float speed, uint min = neg_infinity_v<uint>,
                           uint max = pos_infinity_v<uint>) noexcept = 0;
    virtual bool drag_uint2(const string &label, uint2 *val, float speed, uint min = neg_infinity_v<uint>,
                            uint max = pos_infinity_v<uint>) noexcept = 0;
    virtual bool drag_uint3(const string &label, uint3 *val, float speed, uint min = neg_infinity_v<uint>,
                            uint max = pos_infinity_v<uint>) noexcept = 0;
    virtual bool drag_uint4(const string &label, uint4 *val, float speed, uint min = neg_infinity_v<uint>,
                            uint max = pos_infinity_v<uint>) noexcept = 0;

    virtual bool drag_float(const string &label, float *val, float speed, float min = neg_infinity_v<float>,
                            float max = pos_infinity_v<float>, const char *fmt="%.3f") noexcept = 0;
    virtual bool drag_float2(const string &label, float2 *val, float speed, float min = neg_infinity_v<float>,
                             float max = pos_infinity_v<float>, const char *fmt="%.3f") noexcept = 0;
    virtual bool drag_float3(const string &label, float3 *val, float speed, float min = neg_infinity_v<float>,
                             float max = pos_infinity_v<float>, const char *fmt="%.3f") noexcept = 0;
    virtual bool drag_float4(const string &label, float4 *val, float speed, float min = neg_infinity_v<float>,
                             float max = pos_infinity_v<float>, const char *fmt="%.3f") noexcept = 0;

    bool drag_floatN(const string &label, float *val, uint size,
                     float speed = 0.1, float min = 0, float max = 0, const char *fmt = "%.3f") noexcept;

    virtual bool combo(const string &label, int *current_item,
                       const char *const items[], int item_num) noexcept = 0;

    virtual bool is_item_hovered() noexcept = 0;
    virtual float2 mouse_pos() noexcept = 0;

    bool combo(const string &label, int *current_item, const vector<const char *> &items) noexcept {
        return combo(label, current_item, items.data(), items.size());
    }

    template<size_t N>
    bool combo(const string &label, int *current_item, const array<const char *, N> &items) noexcept {
        return combo(label, current_item, items.data(), items.size());
    }

    virtual ~Widgets() = default;
};

}// namespace ocarina
