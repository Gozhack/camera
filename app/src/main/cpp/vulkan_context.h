#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <vector>
#include <android/native_window.h>
#include <android/log.h>
#include <android/hardware_buffer.h>
#include <android/asset_manager.h>
#include <unordered_map>

#define LOG_TAG "VulkanEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    bool init(ANativeWindow* window, AAssetManager* assetManager);
    void cleanup();
    
    // Phase 3: Texture Rendering
    bool updateCameraTexture(AHardwareBuffer* buffer);
    void drawFrame();
    void triggerFlash() { flashFrames.store(2); }

private:
    std::atomic<int> flashFrames{0};
    // Resource cache for AHardwareBuffers to avoid expensive re-imports
    struct BufferResource {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkDescriptorSet descriptorSet;
    };
    std::unordered_map<AHardwareBuffer*, BufferResource> bufferCache;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    AAssetManager* assetManager = nullptr;
    
    int32_t width = 0;
    int32_t height = 0;
    
    // Rendering resources
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
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

    // UI resources
    VkPipeline uiPipeline = VK_NULL_HANDLE;
    VkPipelineLayout uiPipelineLayout = VK_NULL_HANDLE;
    VkBuffer uiVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory uiVertexMemory = VK_NULL_HANDLE;

    void cleanupSwapchain();
    void recreateSwapchain();

    bool createInstance();
    bool selectPhysicalDevice();
    bool createDevice();
    bool createSurface(ANativeWindow* window);
    bool createSwapchain();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createDescriptorPool();
    bool createGraphicsPipeline();
    bool createUIPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    
    void cleanupCameraResources();
    VkShaderModule createShaderModule(const std::vector<char>& code);
    std::vector<char> loadAsset(const char* filename);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Extension function pointers
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID_ptr = nullptr;
    PFN_vkCreateSamplerYcbcrConversionKHR vkCreateSamplerYcbcrConversionKHR_ptr = nullptr;
    PFN_vkDestroySamplerYcbcrConversionKHR vkDestroySamplerYcbcrConversionKHR_ptr = nullptr;
    
    bool loadExtensions();
};

#endif // VULKAN_CONTEXT_H
