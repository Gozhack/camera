#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <android/native_window.h>
#include <android/log.h>

#include <android/hardware_buffer.h>

#define LOG_TAG "VulkanEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    bool init(ANativeWindow* window);
    void cleanup();
    
    // Phase 3: Texture Rendering
    bool updateCameraTexture(AHardwareBuffer* buffer);
    void drawFrame();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    
    // Rendering resources
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    
    // External texture resources
    VkImage cameraImage = VK_NULL_HANDLE;
    VkDeviceMemory cameraMemory = VK_NULL_HANDLE;
    VkImageView cameraImageView = VK_NULL_HANDLE;
    VkSampler cameraSampler = VK_NULL_HANDLE;
    VkSamplerYcbcrConversion cameraConversion = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // Synchronization
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createSurface(ANativeWindow* window);
    bool createSwapchain();
    
    // New setup methods for Phase 3
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    
    void cleanupCameraResources();

    // Extension function pointers
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID_ptr = nullptr;
    PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR_ptr = nullptr;
    PFN_vkDestroySamplerYcbcrConversionKHR vkDestroySamplerYcbcrConversionKHR_ptr = nullptr;
    
    bool loadExtensions();
};

#endif // VULKAN_CONTEXT_H
