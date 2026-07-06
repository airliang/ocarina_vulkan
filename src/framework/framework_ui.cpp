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



    if (!widgets.push_centered_window(window_title)) {

        return;

    }



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



    widgets.pop_window();

}



void display_frame_info(Widgets& widgets) {

    FrameInfoContext* context = widgets.frame_info_context();

    if (context == nullptr) {

        return;

    }



    if (!widgets.push_window(context->window_title)) {

        return;

    }



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

            const math3d::Vector3D& cam_position = camera->get_position();

            widgets.text(

                "Camera position: (%.3f, %.3f, %.3f)",

                cam_position[0],

                cam_position[1],

                cam_position[2]);

            const math3d::Vector3D cam_forward = math3d::normalize(

                math3d::operator-(camera->get_target(), cam_position));

            widgets.text(

                "Camera forward: (%.3f, %.3f, %.3f)",

                cam_forward[0],

                cam_forward[1],

                cam_forward[2]);

        }



        if (Scene* scene = context->renderer->scene()) {

            widgets.text("Primitives: %zu", scene->primitive_count());

            widgets.text("Visible primitives: %zu", scene->visible_entity_indices().size());

        }

    }



    if (context->extra) {

        context->extra(widgets);

    }



    widgets.pop_window();

}



}// namespace ocarina

