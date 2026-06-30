#pragma once

/// Frame synchronization backend for VulkanDriver.
///
/// Undefined or 0: traditional per-frame fence + binary swapchain semaphores.
/// 1: timeline semaphore for CPU/GPU frame pacing (CPU wait at begin_frame on
///    frame_number - frames_in_flight; signal frame_number on submit). Swapchain
///    acquire/present still use per-slot FrameSync binary semaphores.
///
/// Can also be toggled from CMake: -DOCARINA_VULKAN_FRAME_SYNC_TIMELINE=OFF

#ifndef OCARINA_VULKAN_FRAME_SYNC_TIMELINE
#define OCARINA_VULKAN_FRAME_SYNC_TIMELINE 1
#endif

#if OCARINA_VULKAN_FRAME_SYNC_TIMELINE
#define OCARINA_VULKAN_FRAME_SYNC_TIMELINE_ONLY(...) __VA_ARGS__
#define OCARINA_VULKAN_FRAME_SYNC_TRADITIONAL_ONLY(...)
#else
#define OCARINA_VULKAN_FRAME_SYNC_TIMELINE_ONLY(...)
#define OCARINA_VULKAN_FRAME_SYNC_TRADITIONAL_ONLY(...) __VA_ARGS__
#endif
