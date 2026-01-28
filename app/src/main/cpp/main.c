#define VK_USE_PLATFORM_ANDROID_KHR

#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/configuration.h>
#include <android/native_activity.h>
#include <jni.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include "http_download.h"
#include "url_analyzer.h"

#define LOG_TAG "minimalvulkan"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef struct ShaderBlob {
    uint8_t *data;
    size_t size;
} ShaderBlob;

typedef struct VulkanApp {
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;

    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    uint32_t imageCount;
    VkImage *images;
    VkImageView *imageViews;
    VkFramebuffer *framebuffers;

    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkImage fontImage;
    VkDeviceMemory fontImageMemory;
    VkImageView fontImageView;
    VkSampler fontSampler;

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCapacity;
    uint32_t vertexCount;
    VkSurfaceTransformFlagBitsKHR surfaceTransform;
    bool mirrorX;

    VkCommandPool commandPool;
    VkCommandBuffer *commandBuffers;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;
    VkFence inFlightFence;

    AAssetManager *assetManager;
    ANativeWindow *window;
    struct android_app *androidApp;
    JavaVM *javaVm;
    bool ready;
    float densityScale;

    pthread_mutex_t uiMutex;
    pthread_t workerThread;
    bool workerRunning;
    bool inputActive;
    char urlInput[2048];
    size_t urlLen;
    char statusText[256];
    float progress;

    float uiStartY;
    float uiGlyphW;
    float uiGlyphH;
    float uiGap;
    int uiLineCount;
    int uiKeyboardStartLine;
    int uiKeyboardLineCount;
    float keyboardHeightPx;
    float uiUrlX0;
    float uiUrlX1;
    float uiUrlY0;
    float uiUrlY1;
} VulkanApp;

static VulkanApp *g_app = NULL;

typedef struct Vertex {
    float pos[2];
    float uv[2];
} Vertex;

#define FONT_GLYPH_W 8
#define FONT_GLYPH_H 8
#define FONT_COLS 16
#define FONT_ROWS 6
#define FONT_ATLAS_W (FONT_COLS * FONT_GLYPH_W)
#define FONT_ATLAS_H (FONT_ROWS * FONT_GLYPH_H)

static const uint8_t font8x8_basic[128][8] = {
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x7E,0x81,0xA5,0x81,0xBD,0x99,0x81,0x7E },
    { 0x7E,0xFF,0xDB,0xFF,0xC3,0xE7,0xFF,0x7E },
    { 0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00 },
    { 0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00 },
    { 0x38,0x7C,0x38,0xFE,0xFE,0xD6,0x10,0x38 },
    { 0x10,0x38,0x7C,0xFE,0xFE,0x7C,0x10,0x38 },
    { 0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00 },
    { 0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF },
    { 0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00 },
    { 0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF },
    { 0x0F,0x07,0x0F,0x7D,0xCC,0xCC,0xCC,0x78 },
    { 0x3C,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18 },
    { 0x3F,0x33,0x3F,0x30,0x30,0x70,0xF0,0xE0 },
    { 0x7F,0x63,0x7F,0x63,0x63,0x67,0xE6,0xC0 },
    { 0x99,0x5A,0x3C,0xE7,0xE7,0x3C,0x5A,0x99 },
    { 0x80,0xE0,0xF8,0xFE,0xF8,0xE0,0x80,0x00 },
    { 0x02,0x0E,0x3E,0xFE,0x3E,0x0E,0x02,0x00 },
    { 0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18 },
    { 0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00 },
    { 0x7F,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x00 },
    { 0x3E,0x61,0x3C,0x66,0x66,0x3C,0x86,0x7C },
    { 0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x00 },
    { 0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0xFF },
    { 0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00 },
    { 0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00 },
    { 0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00 },
    { 0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00 },
    { 0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00 },
    { 0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00 },
    { 0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00,0x00 },
    { 0x00,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00 },
    { 0x6C,0x6C,0x48,0x00,0x00,0x00,0x00,0x00 },
    { 0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00 },
    { 0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00 },
    { 0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00 },
    { 0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00 },
    { 0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00 },
    { 0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00 },
    { 0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00 },
    { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00 },
    { 0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30 },
    { 0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00 },
    { 0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00 },
    { 0x7C,0xC6,0xCE,0xD6,0xE6,0xC6,0x7C,0x00 },
    { 0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00 },
    { 0x7C,0xC6,0x0E,0x1C,0x70,0xC0,0xFE,0x00 },
    { 0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00 },
    { 0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00 },
    { 0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00 },
    { 0x3C,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00 },
    { 0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00 },
    { 0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00 },
    { 0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00 },
    { 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00 },
    { 0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30 },
    { 0x0E,0x1C,0x38,0x70,0x38,0x1C,0x0E,0x00 },
    { 0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00 },
    { 0x70,0x38,0x1C,0x0E,0x1C,0x38,0x70,0x00 },
    { 0x7C,0xC6,0x0E,0x1C,0x18,0x00,0x18,0x00 },
    { 0x7C,0xC6,0xDE,0xDE,0xDE,0xC0,0x7C,0x00 },
    { 0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00 },
    { 0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00 },
    { 0x3C,0x66,0xC0,0xC0,0xC0,0x66,0x3C,0x00 },
    { 0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00 },
    { 0xFE,0x62,0x68,0x78,0x68,0x62,0xFE,0x00 },
    { 0xFE,0x62,0x68,0x78,0x68,0x60,0xF0,0x00 },
    { 0x3C,0x66,0xC0,0xC0,0xCE,0x66,0x3A,0x00 },
    { 0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00 },
    { 0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00 },
    { 0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00 },
    { 0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00 },
    { 0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00 },
    { 0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00 },
    { 0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00 },
    { 0x38,0x6C,0xC6,0xC6,0xC6,0x6C,0x38,0x00 },
    { 0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00 },
    { 0x7C,0xC6,0xC6,0xC6,0xD6,0xCC,0x7A,0x00 },
    { 0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00 },
    { 0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00 },
    { 0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00 },
    { 0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00 },
    { 0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00 },
    { 0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00 },
    { 0xC6,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00 },
    { 0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00 },
    { 0xFE,0xC6,0x8C,0x18,0x32,0x66,0xFE,0x00 },
    { 0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00 },
    { 0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00 },
    { 0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00 },
    { 0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF },
    { 0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x00,0x7C,0x06,0x7E,0xC6,0x7E,0x00 },
    { 0xE0,0x60,0x7C,0x66,0x66,0x66,0xDC,0x00 },
    { 0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00 },
    { 0x1C,0x0C,0x7C,0xCC,0xCC,0xCC,0x76,0x00 },
    { 0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00 },
    { 0x3C,0x66,0x60,0xF8,0x60,0x60,0xF0,0x00 },
    { 0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0xF8 },
    { 0xE0,0x60,0x6C,0x76,0x66,0x66,0xE6,0x00 },
    { 0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00 },
    { 0x0C,0x00,0x0C,0x0C,0x0C,0xCC,0xCC,0x78 },
    { 0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00 },
    { 0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00 },
    { 0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xC6,0x00 },
    { 0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00 },
    { 0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00 },
    { 0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0 },
    { 0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E },
    { 0x00,0x00,0xDC,0x76,0x66,0x60,0xF0,0x00 },
    { 0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00 },
    { 0x30,0x30,0xFC,0x30,0x30,0x36,0x1C,0x00 },
    { 0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00 },
    { 0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00 },
    { 0x00,0x00,0xC6,0xD6,0xD6,0xFE,0x6C,0x00 },
    { 0x00,0x00,0xC6,0x6C,0x38,0x6C,0xC6,0x00 },
    { 0x00,0x00,0xC6,0xC6,0xC6,0x7E,0x06,0xFC },
    { 0x00,0x00,0xFE,0x4C,0x18,0x32,0xFE,0x00 },
    { 0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00 },
    { 0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18 },
    { 0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00 },
    { 0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00 },
    { 0x00,0x10,0x38,0x6C,0xC6,0xC6,0xFE,0x00 }
};

static ShaderBlob read_asset(AAssetManager *mgr, const char *path) {
    ShaderBlob blob = {0};
    AAsset *asset = AAssetManager_open(mgr, path, AASSET_MODE_STREAMING);
    if (!asset) {
        LOGE("Failed to open asset: %s", path);
        return blob;
    }
    off_t length = AAsset_getLength(asset);
    if (length <= 0) {
        LOGE("Asset length invalid: %s", path);
        AAsset_close(asset);
        return blob;
    }
    uint8_t *buffer = (uint8_t *)malloc((size_t)length);
    if (!buffer) {
        LOGE("Out of memory reading asset: %s", path);
        AAsset_close(asset);
        return blob;
    }
    int64_t readBytes = AAsset_read(asset, buffer, (size_t)length);
    AAsset_close(asset);
    if (readBytes != length) {
        LOGE("Short read for asset: %s", path);
        free(buffer);
        return blob;
    }
    blob.data = buffer;
    blob.size = (size_t)length;
    return blob;
}

static void free_shader(ShaderBlob *blob) {
    if (blob->data) {
        free(blob->data);
        blob->data = NULL;
        blob->size = 0;
    }
}

static uint32_t find_memory_type(VulkanApp *app, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(app->physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

static bool create_buffer(VulkanApp *app, VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer *buffer,
                          VkDeviceMemory *bufferMemory) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VkResult result = vkCreateBuffer(app->device, &bufferInfo, NULL, buffer);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateBuffer failed: %d", result);
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(app->device, *buffer, &memRequirements);

    uint32_t memoryType = find_memory_type(app, memRequirements.memoryTypeBits, properties);
    if (memoryType == UINT32_MAX) {
        LOGE("No suitable memory type");
        return false;
    }

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryType
    };
    result = vkAllocateMemory(app->device, &allocInfo, NULL, bufferMemory);
    if (result != VK_SUCCESS) {
        LOGE("vkAllocateMemory failed: %d", result);
        return false;
    }

    vkBindBufferMemory(app->device, *buffer, *bufferMemory, 0);
    return true;
}

static VkCommandBuffer begin_single_time_commands(VulkanApp *app) {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = app->commandPool,
        .commandBufferCount = 1
    };
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(app->device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

static void end_single_time_commands(VulkanApp *app, VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };
    vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(app->graphicsQueue);
    vkFreeCommandBuffers(app->device, app->commandPool, 1, &commandBuffer);
}

static bool create_image(VulkanApp *app, uint32_t width, uint32_t height, VkFormat format,
                         VkImageUsageFlags usage, VkImage *image, VkDeviceMemory *memory) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkResult result = vkCreateImage(app->device, &imageInfo, NULL, image);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateImage failed: %d", result);
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(app->device, *image, &memRequirements);

    uint32_t memoryType = find_memory_type(app, memRequirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        LOGE("No suitable image memory type");
        return false;
    }

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = memoryType
    };
    result = vkAllocateMemory(app->device, &allocInfo, NULL, memory);
    if (result != VK_SUCCESS) {
        LOGE("vkAllocateMemory failed: %d", result);
        return false;
    }

    vkBindImageMemory(app->device, *image, *memory, 0);
    return true;
}

static VkImageView create_image_view(VulkanApp *app, VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    VkImageView imageView;
    VkResult result = vkCreateImageView(app->device, &viewInfo, NULL, &imageView);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateImageView failed: %d", result);
        return VK_NULL_HANDLE;
    }
    return imageView;
}

static void transition_image_layout(VulkanApp *app, VkImage image, VkImageLayout oldLayout,
                                    VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = begin_single_time_commands(app);
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
    end_single_time_commands(app, commandBuffer);
}

static void copy_buffer_to_image(VulkanApp *app, VkBuffer buffer, VkImage image,
                                 uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = begin_single_time_commands(app);
    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}
    };
    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);
    end_single_time_commands(app, commandBuffer);
}

static void build_font_atlas(uint8_t *atlas) {
    memset(atlas, 0, FONT_ATLAS_W * FONT_ATLAS_H);
    for (int c = 32; c < 128; ++c) {
        int glyphIndex = c - 32;
        int col = glyphIndex % FONT_COLS;
        int row = glyphIndex / FONT_COLS;
        for (int y = 0; y < FONT_GLYPH_H; ++y) {
            uint8_t bits = font8x8_basic[c][y];
            if (c == 127) {
                bits = 0xFF;
            }
            for (int x = 0; x < FONT_GLYPH_W; ++x) {
                uint8_t on = (bits >> x) & 0x1;
                int atlasX = col * FONT_GLYPH_W + (FONT_GLYPH_W - 1 - x);
                int atlasY = row * FONT_GLYPH_H + (FONT_GLYPH_H - 1 - y);
                atlas[atlasY * FONT_ATLAS_W + atlasX] = on ? 255 : 0;
            }
        }
    }
}

static bool init_instance(VulkanApp *app) {
    if (app->instance != VK_NULL_HANDLE) {
        return true;
    }
    const char *extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MinimalVulkan",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "minimal",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = (uint32_t)(sizeof(extensions) / sizeof(extensions[0])),
        .ppEnabledExtensionNames = extensions
    };

    VkResult result = vkCreateInstance(&createInfo, NULL, &app->instance);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateInstance failed: %d", result);
        return false;
    }
    return true;
}

static bool create_surface(VulkanApp *app) {
    if (app->surface != VK_NULL_HANDLE) {
        return true;
    }
    VkAndroidSurfaceCreateInfoKHR surfaceInfo = {
        .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .window = app->window
    };
    VkResult result = vkCreateAndroidSurfaceKHR(app->instance, &surfaceInfo, NULL, &app->surface);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateAndroidSurfaceKHR failed: %d", result);
        return false;
    }
    return true;
}

static bool pick_device_and_queue(VulkanApp *app) {
    if (app->device != VK_NULL_HANDLE) {
        return true;
    }
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(app->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        LOGE("No Vulkan devices found");
        return false;
    }
    VkPhysicalDevice *devices = (VkPhysicalDevice *)malloc(sizeof(VkPhysicalDevice) * deviceCount);
    vkEnumeratePhysicalDevices(app->instance, &deviceCount, devices);

    VkPhysicalDevice selected = VK_NULL_HANDLE;
    uint32_t selectedQueueFamily = 0;

    for (uint32_t i = 0; i < deviceCount; ++i) {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueCount, NULL);
        VkQueueFamilyProperties *queues =
            (VkQueueFamilyProperties *)malloc(sizeof(VkQueueFamilyProperties) * queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueCount, queues);
        for (uint32_t q = 0; q < queueCount; ++q) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devices[i], q, app->surface, &presentSupported);
            if ((queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
                selected = devices[i];
                selectedQueueFamily = q;
                break;
            }
        }
        free(queues);
        if (selected != VK_NULL_HANDLE) {
            break;
        }
    }
    free(devices);
    if (selected == VK_NULL_HANDLE) {
        LOGE("No suitable Vulkan device found");
        return false;
    }

    app->physicalDevice = selected;
    app->graphicsQueueFamily = selectedQueueFamily;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = app->graphicsQueueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority
    };
    const char *deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledExtensionCount = (uint32_t)(sizeof(deviceExtensions) / sizeof(deviceExtensions[0])),
        .ppEnabledExtensionNames = deviceExtensions
    };

    VkResult result = vkCreateDevice(app->physicalDevice, &deviceCreateInfo, NULL, &app->device);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateDevice failed: %d", result);
        return false;
    }
    vkGetDeviceQueue(app->device, app->graphicsQueueFamily, 0, &app->graphicsQueue);
    return true;
}

static bool create_swapchain(VulkanApp *app) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(app->physicalDevice, app->surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, NULL);
    VkSurfaceFormatKHR *formats =
        (VkSurfaceFormatKHR *)malloc(sizeof(VkSurfaceFormatKHR) * formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(app->physicalDevice, app->surface, &formatCount, formats);

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (uint32_t i = 0; i < formatCount; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosenFormat = formats[i];
            break;
        }
    }
    free(formats);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX || extent.height == UINT32_MAX) {
        int32_t width = ANativeWindow_getWidth(app->window);
        int32_t height = ANativeWindow_getHeight(app->window);
        extent.width = (uint32_t)width;
        extent.height = (uint32_t)height;
    }
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = app->surface,
        .minImageCount = imageCount,
        .imageFormat = chosenFormat.format,
        .imageColorSpace = chosenFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };
    VkResult result = vkCreateSwapchainKHR(app->device, &swapInfo, NULL, &app->swapchain);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateSwapchainKHR failed: %d", result);
        return false;
    }

    vkGetSwapchainImagesKHR(app->device, app->swapchain, &imageCount, NULL);
    app->images = (VkImage *)malloc(sizeof(VkImage) * imageCount);
    vkGetSwapchainImagesKHR(app->device, app->swapchain, &imageCount, app->images);
    app->imageCount = imageCount;
    app->swapchainFormat = chosenFormat.format;
    app->swapchainExtent = extent;
    app->surfaceTransform = caps.currentTransform;
    app->mirrorX = false;
    switch (caps.currentTransform) {
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR:
        case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR:
            app->mirrorX = true;
            break;
        default:
            break;
    }
    LOGE("Swapchain transform=%u mirrorX=%d", caps.currentTransform, app->mirrorX ? 1 : 0);

    app->imageViews = (VkImageView *)malloc(sizeof(VkImageView) * imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = app->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = app->swapchainFormat,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        result = vkCreateImageView(app->device, &viewInfo, NULL, &app->imageViews[i]);
        if (result != VK_SUCCESS) {
            LOGE("vkCreateImageView failed: %d", result);
            return false;
        }
    }
    return true;
}

static bool create_render_pass(VulkanApp *app) {
    VkAttachmentDescription colorAttachment = {
        .format = app->swapchainFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    VkAttachmentReference colorRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef
    };
    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };
    VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };
    VkResult result = vkCreateRenderPass(app->device, &renderPassInfo, NULL, &app->renderPass);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateRenderPass failed: %d", result);
        return false;
    }
    return true;
}

static VkShaderModule create_shader_module(VulkanApp *app, const ShaderBlob *blob) {
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = blob->size,
        .pCode = (const uint32_t *)blob->data
    };
    VkShaderModule module;
    VkResult result = vkCreateShaderModule(app->device, &info, NULL, &module);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateShaderModule failed: %d", result);
        return VK_NULL_HANDLE;
    }
    return module;
}

static bool create_font_resources(VulkanApp *app) {
    uint8_t *atlas = (uint8_t *)malloc(FONT_ATLAS_W * FONT_ATLAS_H);
    if (!atlas) {
        LOGE("Failed to allocate font atlas");
        return false;
    }
    build_font_atlas(atlas);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (!create_buffer(app, FONT_ATLAS_W * FONT_ATLAS_H,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &stagingBuffer, &stagingMemory)) {
        free(atlas);
        return false;
    }

    void *data = NULL;
    vkMapMemory(app->device, stagingMemory, 0, FONT_ATLAS_W * FONT_ATLAS_H, 0, &data);
    memcpy(data, atlas, FONT_ATLAS_W * FONT_ATLAS_H);
    vkUnmapMemory(app->device, stagingMemory);
    free(atlas);

    if (!create_image(app, FONT_ATLAS_W, FONT_ATLAS_H, VK_FORMAT_R8_UNORM,
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      &app->fontImage, &app->fontImageMemory)) {
        return false;
    }
    transition_image_layout(app, app->fontImage, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(app, stagingBuffer, app->fontImage, FONT_ATLAS_W, FONT_ATLAS_H);
    transition_image_layout(app, app->fontImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(app->device, stagingBuffer, NULL);
    vkFreeMemory(app->device, stagingMemory, NULL);

    app->fontImageView = create_image_view(app, app->fontImage, VK_FORMAT_R8_UNORM);
    if (app->fontImageView == VK_NULL_HANDLE) {
        return false;
    }

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
    };
    VkResult result = vkCreateSampler(app->device, &samplerInfo, NULL, &app->fontSampler);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateSampler failed: %d", result);
        return false;
    }

    VkDescriptorSetLayoutBinding samplerBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &samplerBinding
    };
    result = vkCreateDescriptorSetLayout(app->device, &layoutInfo, NULL, &app->descriptorSetLayout);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateDescriptorSetLayout failed: %d", result);
        return false;
    }

    VkDescriptorPoolSize poolSize = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize,
        .maxSets = 1
    };
    result = vkCreateDescriptorPool(app->device, &poolInfo, NULL, &app->descriptorPool);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateDescriptorPool failed: %d", result);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = app->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &app->descriptorSetLayout
    };
    result = vkAllocateDescriptorSets(app->device, &allocInfo, &app->descriptorSet);
    if (result != VK_SUCCESS) {
        LOGE("vkAllocateDescriptorSets failed: %d", result);
        return false;
    }

    VkDescriptorImageInfo imageInfo = {
        .sampler = app->fontSampler,
        .imageView = app->fontImageView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = app->descriptorSet,
        .dstBinding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(app->device, 1, &write, 0, NULL);

    return true;
}

static bool create_vertex_buffer(VulkanApp *app) {
    app->vertexCapacity = 4096;
    VkDeviceSize bufferSize = app->vertexCapacity * sizeof(Vertex);
    return create_buffer(app, bufferSize,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &app->vertexBuffer, &app->vertexBufferMemory);
}

static void append_glyph(Vertex *vertices, uint32_t *count, float x, float y,
                         float w, float h, float u0, float v0, float u1, float v1,
                         float screenW, float screenH, bool mirrorX) {
    if (mirrorX) {
        x = screenW - x - w;
    }
    float yFlipped = screenH - y - h;
    float left = (x / screenW) * 2.0f - 1.0f;
    float right = ((x + w) / screenW) * 2.0f - 1.0f;
    float top = 1.0f - (yFlipped / screenH) * 2.0f;
    float bottom = 1.0f - ((yFlipped + h) / screenH) * 2.0f;

    vertices[(*count)++] = (Vertex){ {left, top}, {u0, v0} };
    vertices[(*count)++] = (Vertex){ {right, top}, {u1, v0} };
    vertices[(*count)++] = (Vertex){ {right, bottom}, {u1, v1} };
    vertices[(*count)++] = (Vertex){ {left, top}, {u0, v0} };
    vertices[(*count)++] = (Vertex){ {right, bottom}, {u1, v1} };
    vertices[(*count)++] = (Vertex){ {left, bottom}, {u0, v1} };
}

static void update_density_scale(struct android_app *app, VulkanApp *vk) {
    if (!app || !app->config) {
        vk->densityScale = 1.0f;
        return;
    }
    int32_t density = AConfiguration_getDensity(app->config);
    if (density <= 0) {
        density = ACONFIGURATION_DENSITY_DEFAULT;
    }
    int32_t baseDensity = ACONFIGURATION_DENSITY_DEFAULT;
    if (baseDensity <= 0) {
        baseDensity = ACONFIGURATION_DENSITY_MEDIUM;
    }
    if (baseDensity <= 0) {
        baseDensity = 160;
    }
    vk->densityScale = (float)density / (float)baseDensity;
}

static void ui_set_status(VulkanApp *app, const char *status) {
    pthread_mutex_lock(&app->uiMutex);
    snprintf(app->statusText, sizeof(app->statusText), "%s", status);
    pthread_mutex_unlock(&app->uiMutex);
}

static void ui_set_progress(VulkanApp *app, float progress) {
    pthread_mutex_lock(&app->uiMutex);
    app->progress = progress;
    pthread_mutex_unlock(&app->uiMutex);
}

static void ui_snapshot(VulkanApp *app, char *urlOut, size_t urlLen,
                        char *statusOut, size_t statusLen, float *progressOut) {
    pthread_mutex_lock(&app->uiMutex);
    snprintf(urlOut, urlLen, "%s", app->urlInput);
    if (app->statusText[0] == '\0') {
        snprintf(statusOut, statusLen, "Idle");
    } else {
        snprintf(statusOut, statusLen, "%s", app->statusText);
    }
    if (progressOut) {
        *progressOut = app->progress;
    }
    pthread_mutex_unlock(&app->uiMutex);
}

static void java_show_keyboard(VulkanApp *app, bool clearText) {
    if (!app || !app->javaVm) {
        return;
    }
    JNIEnv *env = NULL;
    if ((*app->javaVm)->GetEnv(app->javaVm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*app->javaVm)->AttachCurrentThread(app->javaVm, &env, NULL) != JNI_OK) {
            return;
        }
    }
    jclass cls = (*env)->GetObjectClass(env, app->androidApp->activity->clazz);
    jmethodID showMethod = (*env)->GetMethodID(env, cls, "showKeyboard", "(Z)V");
    if (showMethod) {
        (*env)->CallVoidMethod(env, app->androidApp->activity->clazz, showMethod,
                               clearText ? JNI_TRUE : JNI_FALSE);
    }
}

static void java_hide_keyboard(VulkanApp *app) {
    if (!app || !app->javaVm) {
        return;
    }
    JNIEnv *env = NULL;
    if ((*app->javaVm)->GetEnv(app->javaVm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*app->javaVm)->AttachCurrentThread(app->javaVm, &env, NULL) != JNI_OK) {
            return;
        }
    }
    jclass cls = (*env)->GetObjectClass(env, app->androidApp->activity->clazz);
    jmethodID hideMethod = (*env)->GetMethodID(env, cls, "hideKeyboard", "()V");
    if (hideMethod) {
        (*env)->CallVoidMethod(env, app->androidApp->activity->clazz, hideMethod);
    }
}

static char keycode_to_char(int32_t keyCode, int32_t metaState) {
    bool shift = (metaState & AMETA_SHIFT_ON) != 0;
    if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
        char c = (char)('a' + (keyCode - AKEYCODE_A));
        return shift ? (char)('A' + (keyCode - AKEYCODE_A)) : c;
    }
    if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
        char c = (char)('0' + (keyCode - AKEYCODE_0));
        return c;
    }
    switch (keyCode) {
        case AKEYCODE_SPACE:
            return ' ';
        case AKEYCODE_PERIOD:
            return '.';
        case AKEYCODE_COMMA:
            return ',';
        case AKEYCODE_MINUS:
            return shift ? '_' : '-';
        case AKEYCODE_EQUALS:
            return shift ? '+' : '=';
        case AKEYCODE_SLASH:
            return shift ? '?' : '/';
        case AKEYCODE_SEMICOLON:
            return shift ? ':' : ';';
        case AKEYCODE_APOSTROPHE:
            return shift ? '"' : '\'';
        case AKEYCODE_LEFT_BRACKET:
            return shift ? '{' : '[';
        case AKEYCODE_RIGHT_BRACKET:
            return shift ? '}' : ']';
        case AKEYCODE_BACKSLASH:
            return shift ? '|' : '\\';
        case AKEYCODE_AT:
            return '@';
        case AKEYCODE_POUND:
            return '#';
        default:
            return '\0';
    }
}


typedef struct WorkerArgs {
    VulkanApp *app;
    char url[2048];
} WorkerArgs;

static void download_progress(size_t downloaded, size_t total, void *user) {
    VulkanApp *app = (VulkanApp *)user;
    if (total > 0) {
        ui_set_progress(app, (float)downloaded / (float)total);
    }
}

static void *worker_thread(void *arg) {
    WorkerArgs *args = (WorkerArgs *)arg;
    VulkanApp *app = args->app;
    ui_set_progress(app, 0.0f);

    LOGI("Processing URL: %s", args->url);
    ui_set_status(app, "Analyzing URL...");
    
    /* Clear any previous session cookies */
    http_clear_youtube_cookies();

    /* Step 1: Analyze URL and extract media info */
    char err[512] = {0};
    MediaUrl media = {0};
    
    if (!url_analyze(args->url, &media, err, sizeof(err))) {
        LOGE("URL analysis failed: %s", err);
        ui_set_status(app, "Analysis failed");
        app->workerRunning = false;
        free(args);
        return NULL;
    }

    LOGI("Media URL found: %s", media.url);
    ui_set_status(app, "Downloading...");
    ui_set_progress(app, 0.1f);

    /* Step 2: Download the media file */
    HttpBuffer buffer = {0};
    if (!http_get_to_memory(media.url, &buffer, err, sizeof(err))) {
        LOGE("Download failed: %s", err);
        ui_set_status(app, "Download failed");
        app->workerRunning = false;
        free(args);
        return NULL;
    }

    LOGI("Downloaded %zu bytes", buffer.size);
    ui_set_progress(app, 0.9f);
    ui_set_status(app, "Saving...");

    /* Step 3: Save to MediaStore (simplified - just log for now) */
    /* TODO: Integrate with media_store.c for actual file saving */
    LOGI("Would save to MediaStore: %s (%zu bytes)", media.url, buffer.size);
    
    http_free_buffer(&buffer);
    
    ui_set_progress(app, 1.0f);
    ui_set_status(app, "Download complete");

    app->workerRunning = false;
    free(args);
    return NULL;
}

static void start_worker(VulkanApp *app) {
    if (app->workerRunning) {
        return;
    }
    WorkerArgs *args = (WorkerArgs *)malloc(sizeof(WorkerArgs));
    if (!args) {
        ui_set_status(app, "Out of memory");
        return;
    }
    args->app = app;
    pthread_mutex_lock(&app->uiMutex);
    snprintf(args->url, sizeof(args->url), "%s", app->urlInput);
    pthread_mutex_unlock(&app->uiMutex);
    if (args->url[0] == '\0') {
        ui_set_status(app, "Enter a URL");
        free(args);
        return;
    }
    app->workerRunning = (pthread_create(&app->workerThread, NULL, worker_thread, args) == 0);
    if (!app->workerRunning) {
        ui_set_status(app, "Worker start failed");
        free(args);
    }
}

static void update_text_vertices(VulkanApp *app) {
    char url[256];
    char status[128];
    float progress = 0.0f;
    ui_snapshot(app, url, sizeof(url), status, sizeof(status), &progress);

    Vertex *vertices = NULL;
    vkMapMemory(app->device, app->vertexBufferMemory, 0,
                app->vertexCapacity * sizeof(Vertex), 0, (void **)&vertices);
    uint32_t count = 0;

    float scale = 2.0f * app->densityScale;
    float glyphW = FONT_GLYPH_W * scale;
    float glyphH = FONT_GLYPH_H * scale;
    float gap = 12.0f * app->densityScale;
    float screenW = (float)app->swapchainExtent.width;
    float screenH = (float)app->swapchainExtent.height;

    int maxChars = (int)((screenW - glyphW * 2.0f) / glyphW);
    if (maxChars < 10) {
        maxChars = 10;
    }
    float urlBoxWidth = (float)maxChars * glyphW;
    float urlBoxX = (screenW - urlBoxWidth) * 0.5f;
    if (urlBoxX < 0.0f) {
        urlBoxX = 0.0f;
    }

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double nowSec = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    bool caretOn = app->inputActive && (((int)(nowSec * 2.0)) % 2 == 0);
    const char *caretGlyph = caretOn ? "\x7F" : " ";

    char urlLine[160];
    {
        int overhead = 7; /* "URL: <" + ">" + caret/space */
        int maxUrlChars = maxChars - overhead;
        if (maxUrlChars < 4) {
            maxUrlChars = 4;
        }
        if (strlen(url) == 0) {
            if (app->inputActive) {
            snprintf(urlLine, sizeof(urlLine), "URL: <%s>", caretGlyph);
            } else {
                const char *placeholder = "tap to type";
                int len = (int)strlen(placeholder);
                if (len > maxUrlChars) {
                    len = maxUrlChars;
                }
            snprintf(urlLine, sizeof(urlLine), "URL: <%-.*s%s>", len, placeholder,
                     caretGlyph);
            }
        } else {
            int urlLen = (int)strlen(url);
            if (urlLen > maxUrlChars) {
                if (maxUrlChars > 3) {
                    int head = maxUrlChars - 3;
                    snprintf(urlLine, sizeof(urlLine), "URL: <%-.*s...%s>",
                             head, url, caretGlyph);
                } else {
                    snprintf(urlLine, sizeof(urlLine), "URL: <%-.*s%s>",
                             maxUrlChars, url, caretGlyph);
                }
            } else {
                snprintf(urlLine, sizeof(urlLine), "URL: <%-.*s%s>",
                         maxUrlChars, url, caretGlyph);
            }
        }
    }
    char statusLine[160];
    snprintf(statusLine, sizeof(statusLine), "SYS: %.120s", status);
    char actionLine1[64];
    char actionLine2[64];
    snprintf(actionLine1, sizeof(actionLine1), "ENTER=START");
    snprintf(actionLine2, sizeof(actionLine2), "%.0f%%", progress * 100.0f);

    const char *line0 = "BGMDWLDR";
    const char *line1 = urlLine;
    const char *line2 = statusLine;
    const char *line3 = actionLine1;
    const char *line4 = actionLine2;

    char lines[16][160];
    int lineCount = 0;
    int urlLineIndex = -1;
    const char *sourceLines[] = { line0, line1, line2, line3, line4 };
    for (int i = 0; i < 5 && lineCount < 16; ++i) {
        const char *text = sourceLines[i];
        size_t len = strlen(text);
        if (i == 1) {
            snprintf(lines[lineCount++], sizeof(lines[0]), "%s", text);
            urlLineIndex = lineCount - 1;
            continue;
        }
        if ((int)len <= maxChars) {
            snprintf(lines[lineCount++], sizeof(lines[0]), "%s", text);
            if (i == 1) {
                urlLineIndex = lineCount - 1;
            }
            continue;
        }
        size_t start = 0;
        while (start < len && lineCount < 16) {
            size_t end = start + (size_t)maxChars;
            if (end > len) {
                end = len;
            }
            size_t split = end;
            for (size_t j = end; j > start + 5; --j) {
                if (text[j - 1] == ' ') {
                    split = j - 1;
                    break;
                }
            }
            size_t chunk = split - start;
            if (chunk >= sizeof(lines[0])) {
                chunk = sizeof(lines[0]) - 1;
            }
            memcpy(lines[lineCount], text + start, chunk);
            lines[lineCount][chunk] = '\0';
            lineCount++;
            if (i == 1 && urlLineIndex < 0) {
                urlLineIndex = lineCount - 1;
            }
            start = split;
            while (start < len && text[start] == ' ') {
                start++;
            }
        }
    }

    float totalHeight = (float)lineCount * glyphH + (float)(lineCount - 1) * gap;
    float keyboardShift = app->keyboardHeightPx * 0.6f;
    float startY = (screenH - totalHeight) * 0.5f - keyboardShift;
    float minY = glyphH * 0.5f;
    if (startY < minY) {
        startY = minY;
    }

    pthread_mutex_lock(&app->uiMutex);
    app->uiStartY = startY;
    app->uiGlyphW = glyphW;
    app->uiGlyphH = glyphH;
    app->uiGap = gap;
    app->uiLineCount = lineCount;
    app->uiKeyboardStartLine = 0;
    app->uiKeyboardLineCount = 0;
    if (urlLineIndex >= 0) {
        app->uiUrlX0 = urlBoxX;
        app->uiUrlX1 = urlBoxX + urlBoxWidth;
        app->uiUrlY0 = startY + (glyphH + gap) * (float)urlLineIndex;
        app->uiUrlY1 = app->uiUrlY0 + glyphH;
    } else {
        app->uiUrlX0 = app->uiUrlX1 = 0.0f;
        app->uiUrlY0 = app->uiUrlY1 = 0.0f;
    }
    pthread_mutex_unlock(&app->uiMutex);

    for (int l = 0; l < lineCount; ++l) {
        const char *text = lines[l];
        float textW = (float)strlen(text) * glyphW;
        float cursorX = (screenW - textW) * 0.5f;
        if (l == urlLineIndex) {
            cursorX = urlBoxX;
        }
        float y = startY + (glyphH + gap) * (float)l;
        for (size_t i = 0; i < strlen(text); ++i) {
            unsigned char c = (unsigned char)text[i];
            if (c < 32 || c > 127) {
                cursorX += glyphW;
                continue;
            }
            int glyphIndex = (int)c - 32;
            int col = glyphIndex % FONT_COLS;
            int row = glyphIndex / FONT_COLS;
            float u0 = (float)(col * FONT_GLYPH_W) / (float)FONT_ATLAS_W;
            float v0 = (float)(row * FONT_GLYPH_H) / (float)FONT_ATLAS_H;
            float u1 = (float)((col + 1) * FONT_GLYPH_W) / (float)FONT_ATLAS_W;
            float v1 = (float)((row + 1) * FONT_GLYPH_H) / (float)FONT_ATLAS_H;

            if (count + 6 <= app->vertexCapacity) {
                append_glyph(vertices, &count, cursorX, y, glyphW, glyphH,
                             u0, v0, u1, v1,
                             screenW,
                             screenH,
                             app->mirrorX);
            }
            cursorX += glyphW;
        }
    }

    vkUnmapMemory(app->device, app->vertexBufferMemory);
    app->vertexCount = count;
}

static int32_t handle_input(struct android_app *app, AInputEvent *event) {
    VulkanApp *vk = (VulkanApp *)app->userData;
    if (!vk || !vk->ready) {
        return 0;
    }
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            float x = AMotionEvent_getX(event, 0);
            float y = AMotionEvent_getY(event, 0);
            pthread_mutex_lock(&vk->uiMutex);
            float x0 = vk->uiUrlX0;
            float x1 = vk->uiUrlX1;
            float y0 = vk->uiUrlY0;
            float y1 = vk->uiUrlY1;
            pthread_mutex_unlock(&vk->uiMutex);
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
                vk->inputActive = true;
                java_show_keyboard(vk, false);
                ui_set_status(vk, "Typing...");
            } else if (vk->inputActive) {
                java_hide_keyboard(vk);
                vk->inputActive = false;
                ui_set_status(vk, "Idle");
            }
            return 1;
        }
        return 0;
    }
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t action = AKeyEvent_getAction(event);
        if (action != AKEY_EVENT_ACTION_DOWN) {
            return 0;
        }
        (void)event;
        return 0;
    }
    return 0;
}

static bool create_pipeline(VulkanApp *app) {
    ShaderBlob vert = read_asset(app->assetManager, "triangle.vert.spv");
    ShaderBlob frag = read_asset(app->assetManager, "triangle.frag.spv");
    if (!vert.data || !frag.data) {
        free_shader(&vert);
        free_shader(&frag);
        return false;
    }

    VkShaderModule vertModule = create_shader_module(app, &vert);
    VkShaderModule fragModule = create_shader_module(app, &frag);
    free_shader(&vert);
    free_shader(&frag);
    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        return false;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vertModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName = "main"
        }
    };

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    VkVertexInputAttributeDescription attributes[2] = {
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = (uint32_t)offsetof(Vertex, pos)
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = (uint32_t)offsetof(Vertex, uv)
        }
    };
    VkPipelineVertexInputStateCreateInfo vertexInput = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attributes
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)app->swapchainExtent.width,
        .height = (float)app->swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = app->swapchainExtent
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                          VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT |
                          VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment
    };

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &app->descriptorSetLayout
    };
    VkResult result = vkCreatePipelineLayout(app->device, &layoutInfo, NULL, &app->pipelineLayout);
    if (result != VK_SUCCESS) {
        LOGE("vkCreatePipelineLayout failed: %d", result);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample,
        .pColorBlendState = &colorBlending,
        .layout = app->pipelineLayout,
        .renderPass = app->renderPass,
        .subpass = 0
    };
    result = vkCreateGraphicsPipelines(app->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL,
                                       &app->pipeline);
    vkDestroyShaderModule(app->device, vertModule, NULL);
    vkDestroyShaderModule(app->device, fragModule, NULL);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateGraphicsPipelines failed: %d", result);
        return false;
    }
    return true;
}

static bool create_framebuffers(VulkanApp *app) {
    app->framebuffers = (VkFramebuffer *)malloc(sizeof(VkFramebuffer) * app->imageCount);
    for (uint32_t i = 0; i < app->imageCount; ++i) {
        VkImageView attachments[] = { app->imageViews[i] };
        VkFramebufferCreateInfo fbInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = app->renderPass,
            .attachmentCount = 1,
            .pAttachments = attachments,
            .width = app->swapchainExtent.width,
            .height = app->swapchainExtent.height,
            .layers = 1
        };
        VkResult result = vkCreateFramebuffer(app->device, &fbInfo, NULL, &app->framebuffers[i]);
        if (result != VK_SUCCESS) {
            LOGE("vkCreateFramebuffer failed: %d", result);
            return false;
        }
    }
    return true;
}

static bool create_command_pool_and_buffers(VulkanApp *app) {
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = app->graphicsQueueFamily,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    VkResult result = vkCreateCommandPool(app->device, &poolInfo, NULL, &app->commandPool);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateCommandPool failed: %d", result);
        return false;
    }

    app->commandBuffers = (VkCommandBuffer *)malloc(sizeof(VkCommandBuffer) * app->imageCount);
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = app->commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = app->imageCount
    };
    result = vkAllocateCommandBuffers(app->device, &allocInfo, app->commandBuffers);
    if (result != VK_SUCCESS) {
        LOGE("vkAllocateCommandBuffers failed: %d", result);
        return false;
    }

    return true;
}

static void record_command_buffers(VulkanApp *app) {
    VkDeviceSize offsets[] = {0};
    for (uint32_t i = 0; i < app->imageCount; ++i) {
        VkCommandBufferBeginInfo beginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };
        vkBeginCommandBuffer(app->commandBuffers[i], &beginInfo);
        VkClearValue clearColor = { .color = {{0.02f, 0.02f, 0.03f, 1.0f}} };
        VkRenderPassBeginInfo renderPassInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = app->renderPass,
            .framebuffer = app->framebuffers[i],
            .renderArea = {
                .offset = {0, 0},
                .extent = app->swapchainExtent
            },
            .clearValueCount = 1,
            .pClearValues = &clearColor
        };
        vkCmdBeginRenderPass(app->commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(app->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, app->pipeline);
        vkCmdBindDescriptorSets(app->commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                app->pipelineLayout, 0, 1, &app->descriptorSet, 0, NULL);
        vkCmdBindVertexBuffers(app->commandBuffers[i], 0, 1, &app->vertexBuffer, offsets);
        vkCmdDraw(app->commandBuffers[i], app->vertexCount, 1, 0, 0);
        vkCmdEndRenderPass(app->commandBuffers[i]);
        vkEndCommandBuffer(app->commandBuffers[i]);
    }
}

static bool create_sync_objects(VulkanApp *app) {
    VkSemaphoreCreateInfo semInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    VkResult result = vkCreateSemaphore(app->device, &semInfo, NULL, &app->imageAvailableSemaphore);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateSemaphore failed: %d", result);
        return false;
    }
    result = vkCreateSemaphore(app->device, &semInfo, NULL, &app->renderFinishedSemaphore);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateSemaphore failed: %d", result);
        return false;
    }
    result = vkCreateFence(app->device, &fenceInfo, NULL, &app->inFlightFence);
    if (result != VK_SUCCESS) {
        LOGE("vkCreateFence failed: %d", result);
        return false;
    }
    return true;
}

static void cleanup_swapchain(VulkanApp *app) {
    if (app->device == VK_NULL_HANDLE) {
        return;
    }
    if (app->framebuffers) {
        for (uint32_t i = 0; i < app->imageCount; ++i) {
            if (app->framebuffers[i] != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(app->device, app->framebuffers[i], NULL);
            }
        }
        free(app->framebuffers);
        app->framebuffers = NULL;
    }
    if (app->commandBuffers) {
        vkFreeCommandBuffers(app->device, app->commandPool, app->imageCount, app->commandBuffers);
        free(app->commandBuffers);
        app->commandBuffers = NULL;
    }
    if (app->commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(app->device, app->commandPool, NULL);
        app->commandPool = VK_NULL_HANDLE;
    }
    if (app->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(app->device, app->pipeline, NULL);
        app->pipeline = VK_NULL_HANDLE;
    }
    if (app->pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(app->device, app->pipelineLayout, NULL);
        app->pipelineLayout = VK_NULL_HANDLE;
    }
    if (app->renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(app->device, app->renderPass, NULL);
        app->renderPass = VK_NULL_HANDLE;
    }
    if (app->imageViews) {
        for (uint32_t i = 0; i < app->imageCount; ++i) {
            if (app->imageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(app->device, app->imageViews[i], NULL);
            }
        }
        free(app->imageViews);
        app->imageViews = NULL;
    }
    if (app->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(app->device, app->swapchain, NULL);
        app->swapchain = VK_NULL_HANDLE;
    }
    if (app->images) {
        free(app->images);
        app->images = NULL;
    }
    app->imageCount = 0;
}

static void cleanup_device(VulkanApp *app) {
    cleanup_swapchain(app);
    if (app->vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(app->device, app->vertexBuffer, NULL);
        app->vertexBuffer = VK_NULL_HANDLE;
    }
    if (app->vertexBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app->device, app->vertexBufferMemory, NULL);
        app->vertexBufferMemory = VK_NULL_HANDLE;
    }
    if (app->fontSampler != VK_NULL_HANDLE) {
        vkDestroySampler(app->device, app->fontSampler, NULL);
        app->fontSampler = VK_NULL_HANDLE;
    }
    if (app->fontImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(app->device, app->fontImageView, NULL);
        app->fontImageView = VK_NULL_HANDLE;
    }
    if (app->fontImage != VK_NULL_HANDLE) {
        vkDestroyImage(app->device, app->fontImage, NULL);
        app->fontImage = VK_NULL_HANDLE;
    }
    if (app->fontImageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(app->device, app->fontImageMemory, NULL);
        app->fontImageMemory = VK_NULL_HANDLE;
    }
    if (app->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app->device, app->descriptorPool, NULL);
        app->descriptorPool = VK_NULL_HANDLE;
    }
    if (app->descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(app->device, app->descriptorSetLayout, NULL);
        app->descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (app->imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(app->device, app->imageAvailableSemaphore, NULL);
        app->imageAvailableSemaphore = VK_NULL_HANDLE;
    }
    if (app->renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(app->device, app->renderFinishedSemaphore, NULL);
        app->renderFinishedSemaphore = VK_NULL_HANDLE;
    }
    if (app->inFlightFence != VK_NULL_HANDLE) {
        vkDestroyFence(app->device, app->inFlightFence, NULL);
        app->inFlightFence = VK_NULL_HANDLE;
    }
    if (app->device != VK_NULL_HANDLE) {
        vkDestroyDevice(app->device, NULL);
        app->device = VK_NULL_HANDLE;
    }
    if (app->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(app->instance, app->surface, NULL);
        app->surface = VK_NULL_HANDLE;
    }
    if (app->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(app->instance, NULL);
        app->instance = VK_NULL_HANDLE;
    }
}

static bool init_vulkan(VulkanApp *app) {
    if (!init_instance(app)) {
        return false;
    }
    if (!create_surface(app)) {
        return false;
    }
    if (!pick_device_and_queue(app)) {
        return false;
    }
    if (!create_swapchain(app)) {
        return false;
    }
    if (!create_render_pass(app)) {
        return false;
    }
    if (!create_command_pool_and_buffers(app)) {
        return false;
    }
    if (!create_font_resources(app)) {
        return false;
    }
    if (!create_pipeline(app)) {
        return false;
    }
    if (!create_vertex_buffer(app)) {
        return false;
    }
    if (!create_framebuffers(app)) {
        return false;
    }
    update_text_vertices(app);
    record_command_buffers(app);
    if (!create_sync_objects(app)) {
        return false;
    }
    app->ready = true;
    return true;
}

static bool recreate_swapchain(VulkanApp *app) {
    if (!app->window) {
        return false;
    }
    vkDeviceWaitIdle(app->device);
    cleanup_swapchain(app);
    if (!create_swapchain(app)) {
        return false;
    }
    if (!create_render_pass(app)) {
        return false;
    }
    if (!create_command_pool_and_buffers(app)) {
        return false;
    }
    if (!create_pipeline(app)) {
        return false;
    }
    if (!create_framebuffers(app)) {
        return false;
    }
    update_text_vertices(app);
    record_command_buffers(app);
    return true;
}

static void draw_frame(VulkanApp *app) {
    if (!app->ready) {
        return;
    }
    vkWaitForFences(app->device, 1, &app->inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(app->device, 1, &app->inFlightFence);

    update_text_vertices(app);
    record_command_buffers(app);

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(app->device, app->swapchain, UINT64_MAX,
                                            app->imageAvailableSemaphore, VK_NULL_HANDLE,
                                            &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_swapchain(app);
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOGE("vkAcquireNextImageKHR failed: %d", result);
        return;
    }

    VkSemaphore waitSemaphores[] = { app->imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { app->renderFinishedSemaphore };

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &app->commandBuffers[imageIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores
    };
    result = vkQueueSubmit(app->graphicsQueue, 1, &submitInfo, app->inFlightFence);
    if (result != VK_SUCCESS) {
        LOGE("vkQueueSubmit failed: %d", result);
        return;
    }

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = &app->swapchain,
        .pImageIndices = &imageIndex
    };
    result = vkQueuePresentKHR(app->graphicsQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain(app);
    } else if (result != VK_SUCCESS) {
        LOGE("vkQueuePresentKHR failed: %d", result);
    }
}

static void handle_cmd(struct android_app *app, int32_t cmd) {
    VulkanApp *vk = (VulkanApp *)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            vk->window = app->window;
            if (vk->window && !vk->ready) {
                update_density_scale(app, vk);
                init_vulkan(vk);
            }
            break;
        case APP_CMD_CONFIG_CHANGED:
            update_density_scale(app, vk);
            if (vk->ready) {
                update_text_vertices(vk);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            vk->window = NULL;
            vk->ready = false;
            cleanup_swapchain(vk);
            if (vk->surface != VK_NULL_HANDLE && vk->instance != VK_NULL_HANDLE) {
                vkDestroySurfaceKHR(vk->instance, vk->surface, NULL);
                vk->surface = VK_NULL_HANDLE;
            }
            break;
        default:
            break;
    }
}

void android_main(struct android_app *app) {
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;
    VulkanApp vk = {0};
    g_app = &vk;
    vk.assetManager = app->activity->assetManager;
    vk.androidApp = app;
    vk.javaVm = app->activity->vm;
    pthread_mutex_init(&vk.uiMutex, NULL);
    vk.urlInput[0] = '\0';
    vk.urlLen = 0;
    vk.progress = 0.0f;
    vk.workerRunning = false;
    vk.inputActive = false;
    vk.keyboardHeightPx = 0.0f;
    snprintf(vk.statusText, sizeof(vk.statusText), "Idle");
    update_density_scale(app, &vk);
    app->userData = &vk;
    while (true) {
        int events;
        struct android_poll_source *source;
        while (ALooper_pollAll(0, NULL, &events, (void **)&source) >= 0) {
            if (source) {
                source->process(app, source);
            }
            if (app->destroyRequested) {
                pthread_mutex_destroy(&vk.uiMutex);
                g_app = NULL;
                cleanup_device(&vk);
                return;
            }
        }

        if (vk.ready) {
            draw_frame(&vk);
        }
    }
}

JNIEXPORT void JNICALL
Java_com_bgmdwldr_vulkan_MainActivity_nativeOnTextChanged(JNIEnv *env, jclass clazz,
                                                          jstring text) {
    (void)clazz;

    if (!g_app || !text) {
        return;
    }
    const char *utf = (*env)->GetStringUTFChars(env, text, NULL);
    if (!utf) {
        return;
    }
    pthread_mutex_lock(&g_app->uiMutex);
    snprintf(g_app->urlInput, sizeof(g_app->urlInput), "%s", utf);
    g_app->urlLen = strlen(g_app->urlInput);
    pthread_mutex_unlock(&g_app->uiMutex);
    (*env)->ReleaseStringUTFChars(env, text, utf);
}

JNIEXPORT void JNICALL
Java_com_bgmdwldr_vulkan_MainActivity_nativeOnSubmit(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    if (!g_app) {
        return;
    }
    g_app->inputActive = false;
    start_worker(g_app);
}

JNIEXPORT void JNICALL
Java_com_bgmdwldr_vulkan_MainActivity_nativeOnFocus(JNIEnv *env, jclass clazz,
                                                    jboolean focused) {
    (void)env;
    (void)clazz;
    if (!g_app) {
        return;
    }
    g_app->inputActive = focused ? true : false;
    if (!g_app->inputActive) {
        ui_set_status(g_app, "Idle");
    }
}

JNIEXPORT void JNICALL
Java_com_bgmdwldr_vulkan_MainActivity_nativeOnKeyboardHeight(JNIEnv *env, jclass clazz,
                                                             jint heightPx) {
    (void)env;
    (void)clazz;
    if (!g_app) {
        return;
    }
    g_app->keyboardHeightPx = (float)heightPx;
}

/* JNI callbacks for pure C download flow - no WebView */
