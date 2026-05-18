#include "vulkan_context.h"
#include <array>
#include <fstream>
#include <unordered_map>

VulkanContext::~VulkanContext() {
    cleanup();
}

bool VulkanContext::init(ANativeWindow* window, AAssetManager* am) {
    this->assetManager = am;
    this->width = ANativeWindow_getWidth(window);
    this->height = ANativeWindow_getHeight(window);
    LOGI("Initializing Vulkan for high-speed preview: %dx%d", width, height);

    if (!createInstance()) return false;
    if (!createSurface(window)) return false;
    if (!selectPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!loadExtensions()) return false;
    if (!createSwapchain()) return false;
    if (!createRenderPass()) return false;
    if (!createDescriptorPool()) return false;
    if (!createCommandPool()) return false;
    if (!createSyncObjects()) return false;
    
    return true;
}

void VulkanContext::cleanupSwapchain() {
    for (auto fb : framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers.clear();

    vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
    commandBuffers.clear();

    for (auto iv : swapchainImageViews) {
        vkDestroyImageView(device, iv, nullptr);
    }
    swapchainImageViews.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void VulkanContext::cleanup() {
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    
    cleanupCameraResources();
    cleanupSwapchain();

    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(device, graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
    if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    if (imageAvailableSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    if (renderFinishedSemaphore != VK_NULL_HANDLE) vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
    if (inFlightFence != VK_NULL_HANDLE) vkDestroyFence(device, inFlightFence, nullptr);
    
    if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

void VulkanContext::recreateSwapchain() {
    vkDeviceWaitIdle(device);

    cleanupSwapchain();

    createSwapchain();
    createFramebuffers();
    createCommandBuffers();
}


bool VulkanContext::createInstance() {
    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "NDK Camera", 1, "No Engine", 1, VK_API_VERSION_1_1};
    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
    VkInstanceCreateInfo createInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &appInfo, 0, nullptr, (uint32_t)extensions.size(), extensions.data()};
    return vkCreateInstance(&createInfo, nullptr, &instance) == VK_SUCCESS;
}

bool VulkanContext::createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, nullptr, 0, window};
    return vkCreateAndroidSurfaceKHR(instance, &createInfo, nullptr, &surface) == VK_SUCCESS;
}

bool VulkanContext::selectPhysicalDevice() {
    uint32_t count = 0; vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) return false;
    std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(instance, &count, devices.data());
    physicalDevice = devices[0]; return true;
}

bool VulkanContext::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, 0, 1, &priority};
    std::vector<const char*> ext = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME};
    VkDeviceCreateInfo dInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &qInfo, 0, nullptr, (uint32_t)ext.size(), ext.data(), nullptr};
    if (vkCreateDevice(physicalDevice, &dInfo, nullptr, &device) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device, 0, 0, &graphicsQueue); return true;
}

bool VulkanContext::loadExtensions() {
    vkGetAndroidHardwareBufferPropertiesANDROID_ptr = (PFN_vkGetAndroidHardwareBufferPropertiesANDROID)vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID");
    vkCreateSamplerYcbcrConversionKHR_ptr = (PFN_vkCreateSamplerYcbcrConversionKHR)vkGetDeviceProcAddr(device, "vkCreateSamplerYcbcrConversionKHR");
    vkDestroySamplerYcbcrConversionKHR_ptr = (PFN_vkDestroySamplerYcbcrConversionKHR)vkGetDeviceProcAddr(device, "vkDestroySamplerYcbcrConversionKHR");
    return vkGetAndroidHardwareBufferPropertiesANDROID_ptr != nullptr;
}

bool VulkanContext::createSwapchain() {
    VkSwapchainCreateInfoKHR createInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr, 0, surface, 2, VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, {(uint32_t)width, (uint32_t)height}, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, VK_NULL_HANDLE};
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS) return false;
    uint32_t count; vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    swapchainImages.resize(count); vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());
    for (auto img : swapchainImages) {
        VkImageViewCreateInfo ivInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, img, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VkImageView iv; vkCreateImageView(device, &ivInfo, nullptr, &iv);
        swapchainImageViews.push_back(iv);
    }
    return true;
}

bool VulkanContext::createRenderPass() {
    VkAttachmentDescription color = {0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference colorRef = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &colorRef};
    VkRenderPassCreateInfo rpInfo = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1, &color, 1, &subpass};
    return vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass) == VK_SUCCESS;
}

bool VulkanContext::createDescriptorPool() {
    VkDescriptorPoolSize poolSize = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10}; // Increased for buffer rotation
    VkDescriptorPoolCreateInfo poolInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 10, 1, &poolSize};
    return vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) == VK_SUCCESS;
}

std::vector<char> VulkanContext::loadAsset(const char* filename) {
    AAsset* asset = AAssetManager_open(assetManager, filename, AASSET_MODE_BUFFER);
    if (!asset) return {};
    size_t size = AAsset_getLength(asset);
    std::vector<char> buffer(size);
    AAsset_read(asset, buffer.data(), size);
    AAsset_close(asset);
    return buffer;
}

VkShaderModule VulkanContext::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo smInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.size(), (uint32_t*)code.data()};
    VkShaderModule mod; vkCreateShaderModule(device, &smInfo, nullptr, &mod); return mod;
}

bool VulkanContext::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &cameraSampler};
    VkDescriptorSetLayoutCreateInfo layoutInfo = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 1, &samplerBinding};
    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) == VK_SUCCESS;
}

bool VulkanContext::createGraphicsPipeline() {
    auto vertCode = loadAsset("shaders/camera.vert.spv");
    auto fragCode = loadAsset("shaders/camera.frag.spv");
    VkShaderModule vertMod = createShaderModule(vertCode);
    VkShaderModule fragMod = createShaderModule(fragCode);
    VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertMod, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main"}
    };
    VkPipelineVertexInputStateCreateInfo vInput = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo iAssembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE};
    VkViewport vp = {0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {(uint32_t)width, (uint32_t)height}};
    VkPipelineViewportStateCreateInfo vpState = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &vp, 1, &sc};
    VkPipelineRasterizationStateCreateInfo rast = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE};
    VkPipelineMultisampleStateCreateInfo multi = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cbAtt = {VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0xF};
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &cbAtt};
    VkPipelineLayoutCreateInfo lInfo = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &descriptorSetLayout};
    vkCreatePipelineLayout(device, &lInfo, nullptr, &pipelineLayout);
    VkGraphicsPipelineCreateInfo pInfo = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0, 2, stages, &vInput, &iAssembly, nullptr, &vpState, &rast, &multi, nullptr, &cb, nullptr, pipelineLayout, renderPass, 0};
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pInfo, nullptr, &graphicsPipeline);
    vkDestroyShaderModule(device, vertMod, nullptr); vkDestroyShaderModule(device, fragMod, nullptr);
    return true;
}

bool VulkanContext::createFramebuffers() {
    for (auto iv : swapchainImageViews) {
        VkFramebufferCreateInfo fbInfo = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, renderPass, 1, &iv, (uint32_t)width, (uint32_t)height, 1};
        VkFramebuffer fb; vkCreateFramebuffer(device, &fbInfo, nullptr, &fb); framebuffers.push_back(fb);
    }
    return true;
}

bool VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo pInfo = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 0};
    return vkCreateCommandPool(device, &pInfo, nullptr, &commandPool) == VK_SUCCESS;
}

bool VulkanContext::createCommandBuffers() {
    commandBuffers.resize(framebuffers.size());
    VkCommandBufferAllocateInfo aInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, (uint32_t)commandBuffers.size()};
    return vkAllocateCommandBuffers(device, &aInfo, commandBuffers.data()) == VK_SUCCESS;
}

bool VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo sInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fInfo = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};
    return vkCreateSemaphore(device, &sInfo, nullptr, &imageAvailableSemaphore) == VK_SUCCESS && vkCreateSemaphore(device, &sInfo, nullptr, &renderFinishedSemaphore) == VK_SUCCESS && vkCreateFence(device, &fInfo, nullptr, &inFlightFence) == VK_SUCCESS;
}

void VulkanContext::cleanupCameraResources() {
    for (auto& pair : bufferCache) {
        vkDestroyImageView(device, pair.second.view, nullptr);
        vkDestroyImage(device, pair.second.image, nullptr);
        vkFreeMemory(device, pair.second.memory, nullptr);
        AHardwareBuffer_release(pair.first);
        // Descriptor sets are cleaned up with the pool
    }
    bufferCache.clear();
    if (cameraSampler != VK_NULL_HANDLE) vkDestroySampler(device, cameraSampler, nullptr);
    if (cameraConversion != VK_NULL_HANDLE && vkDestroySamplerYcbcrConversionKHR_ptr) vkDestroySamplerYcbcrConversionKHR_ptr(device, cameraConversion, nullptr);
    cameraSampler = VK_NULL_HANDLE; cameraConversion = VK_NULL_HANDLE; descriptorSet = VK_NULL_HANDLE;
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps; vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) { if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) return i; }
    return 0;
}

bool VulkanContext::updateCameraTexture(AHardwareBuffer* buffer) {
    if (buffer == nullptr) return false;

    // Check if buffer is already cached
    auto it = bufferCache.find(buffer);
    if (it != bufferCache.end()) {
        descriptorSet = it->second.descriptorSet;
        return true;
    }

    // New buffer detected (initialization or new buffer in rotation)
    AHardwareBuffer_Desc desc; AHardwareBuffer_describe(buffer, &desc);
    VkAndroidHardwareBufferFormatPropertiesANDROID bFP = {VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID bP = {VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &bFP};
    vkGetAndroidHardwareBufferPropertiesANDROID_ptr(device, buffer, &bP);

    VkExternalFormatANDROID extFormat = {VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID, nullptr, bFP.externalFormat};
    
    // Create sampler/conversion ONLY ONCE
    if (cameraConversion == VK_NULL_HANDLE) {
        VkSamplerYcbcrConversionCreateInfo cInfo = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO, &extFormat, VK_FORMAT_UNDEFINED, bFP.suggestedYcbcrModel, bFP.suggestedYcbcrRange, bFP.samplerYcbcrConversionComponents, bFP.suggestedXChromaOffset, bFP.suggestedYChromaOffset, VK_FILTER_LINEAR, VK_FALSE};
        vkCreateSamplerYcbcrConversionKHR_ptr(device, &cInfo, nullptr, &cameraConversion);

        VkSamplerYcbcrConversionInfo sConvInfo = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO, nullptr, cameraConversion};
        VkSamplerCreateInfo sInfo = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, &sConvInfo, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, VK_FALSE, 1.0f, VK_FALSE, VK_COMPARE_OP_ALWAYS, 0.0f, 0.0f, VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK, VK_FALSE};
        vkCreateSampler(device, &sInfo, nullptr, &cameraSampler);
        
        if (!createDescriptorSetLayout()) return false;
        if (!createGraphicsPipeline()) return false;
        if (!createFramebuffers()) return false;
        if (!createCommandBuffers()) return false;
    }

    // Import this specific buffer
    BufferResource res = {};
    VkExternalMemoryImageCreateInfo eMInfo = {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, &extFormat, VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
    VkImageCreateInfo iInfo = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &eMInfo, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_UNDEFINED, {desc.width, desc.height, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED};
    vkCreateImage(device, &iInfo, nullptr, &res.image);

    VkImportAndroidHardwareBufferInfoANDROID impInfo = {VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, nullptr, buffer};
    VkMemoryDedicatedAllocateInfo dAllocInfo = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, &impInfo, res.image};
    VkMemoryAllocateInfo allocInfo = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &dAllocInfo, bP.allocationSize, findMemoryType(bP.memoryTypeBits, 0)};
    vkAllocateMemory(device, &allocInfo, nullptr, &res.memory);
    vkBindImageMemory(device, res.image, res.memory, 0);

    VkSamplerYcbcrConversionInfo sConvInfo = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO, nullptr, cameraConversion};
    VkImageViewCreateInfo vInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, &sConvInfo, 0, res.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_UNDEFINED, {}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCreateImageView(device, &vInfo, nullptr, &res.view);

    VkDescriptorSetAllocateInfo dsAlloc = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &descriptorSetLayout};
    vkAllocateDescriptorSets(device, &dsAlloc, &res.descriptorSet);
    
    VkDescriptorImageInfo imgDesc = {cameraSampler, res.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, res.descriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgDesc};
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    bufferCache[buffer] = res;
    descriptorSet = res.descriptorSet;
    return true;
}

void VulkanContext::drawFrame() {
    if (descriptorSet == VK_NULL_HANDLE) return;
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &inFlightFence);

    uint32_t idx; 
    VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &idx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOGE("Failed to acquire swap chain image!");
        return;
    }

    vkResetCommandBuffer(commandBuffers[idx], 0);
    VkCommandBufferBeginInfo bInfo = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(commandBuffers[idx], &bInfo);
    VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rpB = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, renderPass, framebuffers[idx], {{0, 0}, {(uint32_t)width, (uint32_t)height}}, 1, &clear};
    vkCmdBeginRenderPass(commandBuffers[idx], &rpB, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffers[idx], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
    vkCmdBindDescriptorSets(commandBuffers[idx], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffers[idx], 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffers[idx]); vkEndCommandBuffer(commandBuffers[idx]);
    
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submitInfo = {VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, &imageAvailableSemaphore, waitStages, 1, &commandBuffers[idx], 1, &renderFinishedSemaphore};
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);
    VkPresentInfoKHR pInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &renderFinishedSemaphore, 1, &swapchain, &idx};
    
    result = vkQueuePresentKHR(graphicsQueue, &pInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        LOGE("Failed to present swap chain image!");
    }
}
