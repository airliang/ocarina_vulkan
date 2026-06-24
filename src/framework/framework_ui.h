#pragma once



#include "core/header.h"

#include "core/stl.h"

#include "math/basic_types.h"

#include "widgets.h"



namespace ocarina {



class LoadingProgressListener;

class Renderer;

class Device;



struct FrameInfoContext {

    Renderer* renderer = nullptr;

    Device* device = nullptr;

    string window_title;

    ocarina::function<void(Widgets&)> extra;

};



OC_FRAMEWORK_API void display_loading_progress(

    Widgets& widgets,

    const LoadingProgressListener* progress_listener,

    double time_seconds,

    const string& window_title = "Loading");



OC_FRAMEWORK_API void display_frame_info(Widgets& widgets);



}// namespace ocarina

