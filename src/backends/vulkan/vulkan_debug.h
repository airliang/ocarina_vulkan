//
// Created by Zero on 06/06/2022.
//

#pragma once
#include <vulkan/vulkan.h>
#include "rhi/graphics_descriptions.h"

namespace ocarina {
	VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data);

	VkDebugUtilsMessageSeverityFlagsEXT validation_debug_message_severity();
	VkDebugUtilsMessageTypeFlagsEXT validation_debug_message_types();

	// Load debug function pointers and set debug callback
	void setup_debugging(VkInstance instance);
	// Clear debug callback
	void free_debug_callback(VkInstance instance);
}
