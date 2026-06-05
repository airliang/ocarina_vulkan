//
// Created by Zero on 2024/3/16.
//

#pragma once

#include "widgets.h"
#include "core/image.h"

namespace ocarina {

class ImGuiWidgets : public Widgets {
public:
    explicit ImGuiWidgets(SDLWindow *window);

    void push_item_width(int width) noexcept override;
    void pop_item_width() noexcept override;

    void begin_tool_tip() noexcept override;
    void end_tool_tip() noexcept override;

    void begin_disabled() noexcept override;
    void end_disabled() noexcept override;

    bool radio_button(const std::string &label, bool active) noexcept override;

    void image(uint tex_handle, uint2 size, float2 uv0, float2 uv1) noexcept override;
    void image(const Image &image) noexcept override;
    void image(const ImageView &image_view) noexcept override;
    uint2 node_size() noexcept override;

    bool push_window(const string &label) noexcept override;
    bool push_window(const string &label, WindowFlag flag) noexcept override;
    void pop_window() noexcept override;

    bool tree_node(const string &label) noexcept override;
    void tree_pop() noexcept override;

    void push_id(char *str) noexcept override;
    void pop_id() noexcept override;

    bool folding_header(const string &label) noexcept override;

    bool begin_main_menu_bar() noexcept override;
    void end_main_menu_bar() noexcept override;

    bool begin_menu_bar() noexcept override;
    bool begin_menu(const string &label) noexcept override;
    bool menu_item(const string &label) noexcept override;
    void end_menu() noexcept override;
    void end_menu_bar() noexcept override;

    void text(const char *format, ...) noexcept override;
    bool input_text(const std::string &label, char *buf, size_t buf_size) noexcept override;
    void text_wrapped(const char *format, ...) noexcept override;
    bool check_box(const string &label, bool *val) noexcept override;

    bool slider_float(const string &label, float *val, float min, float max) noexcept override;
    bool slider_float2(const string &label, float2 *val, float min, float max) noexcept override;
    bool slider_float3(const string &label, float3 *val, float min, float max) noexcept override;
    bool slider_float4(const string &label, float4 *val, float min, float max) noexcept override;

    bool slider_int(const string &label, int *val, int min, int max) noexcept override;
    bool slider_int2(const string &label, int2 *val, int min, int max) noexcept override;
    bool slider_int3(const string &label, int3 *val, int min, int max) noexcept override;
    bool slider_int4(const string &label, int4 *val, int min, int max) noexcept override;

    bool slider_uint(const string &label, uint *val, uint min, uint max) noexcept override;
    bool slider_uint2(const string &label, uint2 *val, uint min, uint max) noexcept override;
    bool slider_uint3(const string &label, uint3 *val, uint min, uint max) noexcept override;
    bool slider_uint4(const string &label, uint4 *val, uint min, uint max) noexcept override;

    bool color_edit(const string &label, float3 *val) noexcept override;
    bool color_edit(const string &label, float4 *val) noexcept override;

    bool button(const string &label, uint2 size) noexcept override;
    bool button(const string &label) noexcept override;

    void same_line() noexcept override;
    void new_line() noexcept override;

    bool input_int(const string &label, int *val) noexcept override;
    bool input_int(const string &label, int *val, int step, int step_fast) noexcept override;
    bool input_int2(const string &label, ocarina::int2 *val) noexcept override;
    bool input_int3(const string &label, ocarina::int3 *val) noexcept override;
    bool input_int4(const string &label, ocarina::int4 *val) noexcept override;

    bool input_uint(const string &label, uint *val) noexcept override;
    bool input_uint(const string &label, uint *val, uint step, uint step_fast) noexcept override;
    bool input_uint2(const string &label, ocarina::uint2 *val) noexcept override;
    bool input_uint3(const string &label, ocarina::uint3 *val) noexcept override;
    bool input_uint4(const string &label, ocarina::uint4 *val) noexcept override;

    bool input_float(const string &label, float *val) noexcept override;
    bool input_float(const string &label, float *val, float step, float step_fast) noexcept override;
    bool input_float2(const string &label, ocarina::float2 *val) noexcept override;
    bool input_float3(const string &label, ocarina::float3 *val) noexcept override;
    bool input_float4(const string &label, ocarina::float4 *val) noexcept override;

    bool drag_int(const string &label, int *val, float speed, int min, int max) noexcept override;
    bool drag_int2(const string &label, ocarina::int2 *val, float speed, int min, int max) noexcept override;
    bool drag_int3(const string &label, ocarina::int3 *val, float speed, int min, int max) noexcept override;
    bool drag_int4(const string &label, ocarina::int4 *val, float speed, int min, int max) noexcept override;

    bool drag_uint(const string &label, ocarina::uint *val, float speed, ocarina::uint min, ocarina::uint max) noexcept override;
    bool drag_uint2(const string &label, ocarina::uint2 *val, float speed, ocarina::uint min, ocarina::uint max) noexcept override;
    bool drag_uint3(const string &label, ocarina::uint3 *val, float speed, ocarina::uint min, ocarina::uint max) noexcept override;
    bool drag_uint4(const string &label, ocarina::uint4 *val, float speed, ocarina::uint min, ocarina::uint max) noexcept override;

    bool drag_float(const string &label, float *val, float speed, float min, float max, const char *fmt) noexcept override;
    bool drag_float2(const string &label, ocarina::float2 *val, float speed, float min, float max, const char *fmt) noexcept override;
    bool drag_float3(const string &label, ocarina::float3 *val, float speed, float min, float max, const char *fmt) noexcept override;
    bool drag_float4(const string &label, ocarina::float4 *val, float speed, float min, float max, const char *fmt) noexcept override;

    bool combo(const std::string &label, int *current_item, const char *const *items, int item_num) noexcept override;
    bool is_item_hovered() noexcept override;
    float2 mouse_pos() noexcept override;
};

}// namespace ocarina
