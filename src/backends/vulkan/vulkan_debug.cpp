
#include "vulkan_debug.h"
#include "core/logging.h"
#include <sstream>

namespace ocarina {
	PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
	PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
	VkDebugUtilsMessengerEXT debug_utils_messenger;

	VkDebugUtilsMessageSeverityFlagsEXT validation_debug_message_severity() {
		return VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	}

	VkDebugUtilsMessageTypeFlagsEXT validation_debug_message_types() {
		return VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
			| VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	}

	namespace {

	std::string format_validation_message(const VkDebugUtilsMessengerCallbackDataEXT* callback_data) {
		std::ostringstream stream;
		stream << "[Vulkan validation] ";
		if (callback_data->pMessageIdName) {
			stream << "[" << callback_data->messageIdNumber << "]["
				<< callback_data->pMessageIdName << "] : "
				<< callback_data->pMessage;
		} else {
			stream << "[" << callback_data->messageIdNumber << "] : "
				<< callback_data->pMessage;
		}
		return stream.str();
	}

	} // namespace

	VKAPI_ATTR VkBool32 VKAPI_CALL debug_message_callback(
		VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_type,
		const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
		void* user_data)
	{
		const std::string message = format_validation_message(callback_data);

#if defined(__ANDROID__)
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			LOGE("%s", message.c_str());
		} else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
			LOGW("%s", message.c_str());
		} else {
			LOGD("%s", message.c_str());
		}
#else
		auto& log = core::logger();
		if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
			log.error(message);
		} else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
			log.warn(message);
		} else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
			log.info(message);
		} else {
			log.debug(message);
		}
		log.flush();
#endif

		// The return value of this callback controls whether the Vulkan call that caused the validation message will be aborted or not
		// We return VK_FALSE as we DON'T want Vulkan calls that cause a validation message to abort
		// If you instead want to have calls abort, pass in VK_TRUE and the function will return VK_ERROR_VALIDATION_FAILED_EXT
		return VK_FALSE;
	}

	void setup_debugging(VkInstance instance)
	{
		vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
		vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

		VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
		debugUtilsMessengerCI.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		debugUtilsMessengerCI.messageSeverity = validation_debug_message_severity();
		debugUtilsMessengerCI.messageType = validation_debug_message_types();
		debugUtilsMessengerCI.pfnUserCallback = debug_message_callback;
		VkResult result = vkCreateDebugUtilsMessengerEXT(instance, &debugUtilsMessengerCI, nullptr, &debug_utils_messenger);
		assert(result == VK_SUCCESS);
	}

	void free_debug_callback(VkInstance instance)
	{
		if (debug_utils_messenger != VK_NULL_HANDLE)
		{
			vkDestroyDebugUtilsMessengerEXT(instance, debug_utils_messenger, nullptr);
		}
	}

}// namespace ocarina


