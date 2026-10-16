#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "RHI/VmaConfig.h"   // 必须在 vk_mem_alloc.h 之前
#include <vk_mem_alloc.h>
#include "RHI/VmaNaming.h"   // 必须在 vk_mem_alloc.h 之后
#include <vector>
#include <optional>
#include <string>

struct QueueFamilyIndices 
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() 
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

class Device
{
public:
    Device(GLFWwindow* window);
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    
    VkDevice getDevice() { return device; }
    VkPhysicalDevice getPhysicalDevice() { return physicalDevice; }
    VkSurfaceKHR getSurface() { return surface; }
    VkQueue getGraphicsQueue() { return graphicsQueue; }
	VkQueue getPresentQueue() { return presentQueue; }
	VkInstance getInstance() { return instance; }
	VmaAllocator getVmaAllocator() { return vmaAllocator; }

	// 打印 VMA 统计，并返回"还有多少个分配没释放"。
	// 为什么需要：vmaDestroyAllocator 在前者不为 0 时会直接断言崩掉，而断言
	// 只说"有分配没释放"，不说是谁。在析构链中间插一次调用，就能把范围缩到
	// "两次调用之间被销毁的那些对象"——比读代码猜快得多。
	size_t reportVmaStats(const char* stage);
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    VkFormat findDepthFormat();
    QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }
    struct SwapChainSupportDetails 
    {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();

    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    GLFWwindow* window;
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device;
	VkQueue graphicsQueue;
	VkQueue presentQueue;
	VmaAllocator vmaAllocator;

    const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};