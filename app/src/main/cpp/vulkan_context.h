#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <android/native_window.h>
#include <android/log.h>

#define LOG_TAG "VulkanEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    bool init(ANativeWindow* window);
    void cleanup();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;

    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createSurface(ANativeWindow* window);
    bool createSwapchain();
};

#endif // VULKAN_CONTEXT_H
