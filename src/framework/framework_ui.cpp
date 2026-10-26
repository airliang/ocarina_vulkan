#include "framework_ui.h"

#include "loading_progress_listener.h"
#include "renderer.h"
#include "camera.h"
#include "scene.h"
#include "rhi/device.h"
#include "math.h"

namespace ocarina {

void display_loading_progress(
    Widgets& widgets,
    const LoadingProgressListener* progress_listener,
    double time_seconds,
    const string& window_title) {
    const LoadingProgressListener::Snapshot progress =
        progress_listener != nullptr ? progress_listener->snapshot() : LoadingProgressListener::Snapshot{};

    // ImGui: End()/pop_window() is mandatory even when Begin returns false
    // (e.g. OS window minimized → DisplaySize 0 → Begin clips and returns false).
    const bool open = widgets.push_centered_window(window_title);
    if (open) {
        if (!progress.title.empty()) {
            widgets.text(progress.title.c_str());
        } else {
            widgets.text("Loading");
        }

        const int dot_count = static_cast<int>(time_seconds * 2.0) % 4;
        char dots[5] = "....";
        dots[dot_count] = '\0';
        widgets.same_line();
        widgets.text(dots);

        if (!progress.phase.empty()) {
            widgets.text(progress.phase.c_str());
        }

        widgets.new_line();
        if (progress.progress >= 0.f) {
            widgets.progress_bar(progress.progress);
            if (progress.total_steps > 0) {
                widgets.text("%u / %u", progress.current_step, progress.total_steps);
            }
        } else {
            widgets.progress_bar(-1.f);
        }

        if (!progress.message.empty()) {
            widgets.new_line();
            widgets.text_wrapped("%s", progress.message.c_str());
        }
    }
    widgets.pop_window();
}

void display_frame_info(Widgets& widgets) {
    FrameInfoContext* context = widgets.frame_info_context();
    if (context == nullptr) {
        return;
    }

    // ImGui: End()/pop_window() is mandatory even when Begin returns false
    // (e.g. OS window minimized → DisplaySize 0 → Begin clips and returns false).
    const bool open = widgets.push_window(context->window_title);
    if (open) {
        if (context->renderer != nullptr) {
            const double dt = context->renderer->dt();
            const float fps = dt > 0.0 ? static_cast<float>(1.0 / dt) : 0.f;
            widgets.text("FPS: %.2f", fps);
        }

        if (context->device != nullptr) {
            widgets.text("GPU frame: %.3f ms", context->device->gpu_frame_time_ms());
        }

        if (context->renderer != nullptr) {
            if (Camera* camera = context->renderer->camera()) {
                const ktm::fvec3& cam_position = camera->get_position();
                widgets.text(
                    "Camera position: (%.3f, %.3f, %.3f)",
                    cam_position[0],
                    cam_position[1],
                    cam_position[2]);

                const ktm::fvec3 cam_forward = ktm::normalize(camera->get_target() - cam_position);
                widgets.text(
                    "Camera forward: (%.3f, %.3f, %.3f)",
                    cam_forward[0],
                    cam_forward[1],
                    cam_forward[2]);

                widgets.new_line();
                widgets.text("Camera controls");

                float move_speed = camera->get_move_speed();
                if (widgets.slider_float("Move speed", &move_speed, 0.01f, 50.0f)) {
                    camera->set_move_speed(move_speed);
                }

                // Look / pan sensitivities are small radians-per-pixel values.
                float look_sensitivity = camera->get_look_sensitivity();
                if (widgets.slider_float("Look / rotate speed", &look_sensitivity, 0.0005f, 0.05f)) {
                    camera->set_look_sensitivity(look_sensitivity);
                }

                float pan_sensitivity = camera->get_pan_sensitivity();
                if (widgets.slider_float("Pan speed", &pan_sensitivity, 0.0005f, 0.05f)) {
                    camera->set_pan_sensitivity(pan_sensitivity);
                }
            }

            if (Scene* scene = context->renderer->scene()) {
                widgets.text("Primitives: %zu", scene->primitive_count());
                widgets.text("Visible primitives: %zu", context->renderer->visible_entity_indices().size());
            }
        }

        if (context->extra) {
            context->extra(widgets);
        }
    }
    widgets.pop_window();
}

}// namespace ocarina
