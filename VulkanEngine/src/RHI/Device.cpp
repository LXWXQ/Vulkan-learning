#include "Device.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include <cstring>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace 
{
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) 
    {
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) 
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } 
        else 
        {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) 
    {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) 
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,VkDebugUtilsMessageTypeFlagsEXT messageType,const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,void* pUserData) 
    {
        std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }
}

Device::Device(GLFWwindow* window) : window(window) 
{
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

Device::~Device() 
{
    vmaDestroyAllocator(vmaAllocator);
    vkDestroyDevice(device, nullptr);

    if (enableValidationLayers) 
    {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
}

size_t Device::reportVmaStats(const char* stage)
{
    // 这个函数只在排查内存泄漏时手工插入调用（工作区里不留调用点）——
    // vmaDestroyAllocator 看到"有分配没释放"会直接断言崩掉，而断言不说是谁，
    // 所以在析构链各处插一次、对比 allocationCount 的变化，就能把范围缩到
    // "两次调用之间被销毁的那些对象"。配合 SENTINEL_VMA_LEAK_CHECK=ON
    // 打开分配命名，通常一轮就能定位。
    VmaBudget budget[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(vmaAllocator, budget);

    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &props);

    size_t totalBlocks = 0, totalAllocs = 0, totalUnused = 0;
    for (uint32_t i = 0; i < props.memoryHeapCount; ++i)
    {
        totalBlocks += budget[i].statistics.blockCount;
        totalAllocs += budget[i].statistics.allocationCount;
        const VkDeviceSize unused = budget[i].statistics.blockBytes - budget[i].statistics.allocationBytes;
        totalUnused += static_cast<size_t>(unused);
    }

    std::cout << "[VMA] " << stage
              << " | blockCount=" << totalBlocks
              << " allocationCount=" << totalAllocs
              << " 块内未用字节=" << totalUnused << std::endl;

    return totalAllocs;
}

void Device::createInstance() 
{
    if (enableValidationLayers && !checkValidationLayerSupport()) 
    {
        throw std::runtime_error("Validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    // 目标平台只有 Windows 桌面（用户已确认），且 RTX 2070 支持到 Vulkan 1.4，
    // 因此直接按 1.3 起：dynamic rendering / synchronization2 都是 1.3 核心特性。
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidationLayers) 
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } 
    else 
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) 
    {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void Device::setupDebugMessenger() 
{
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) 
    {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}

void Device::createSurface() 
{
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) 
    {
        throw std::runtime_error("Failed to create window surface!");
    }
}

void Device::pickPhysicalDevice() 
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) 
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& dev : devices) 
    {
        if (isDeviceSuitable(dev)) 
        {
            physicalDevice = dev;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) 
    {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void Device::createLogicalDevice() 
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value(),
        indices.computeFamily.value()
    };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // ---- 查询并启用 Vulkan 1.3 / 1.2 特性 ----
    // dynamicRendering：让渲染图彻底摆脱 VkRenderPass + VkFramebuffer
    // synchronization2：用 vkCmdPipelineBarrier2 写屏障，比旧接口清爽得多
    // descriptorIndexing（D-3）：bindless 的前提 —— 着色器用运行时索引去采样
    //   一个大纹理数组，材质切换不再需要重绑描述符集
    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supported13.pNext = &supported12;      // 1.3 在前、1.2 挂后面
    VkPhysicalDeviceFeatures2 supportedFeatures{};
    supportedFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures.pNext = &supported13;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

    if (!supported13.dynamicRendering || !supported13.synchronization2)
    {
        throw std::runtime_error(
            "GPU 不支持 dynamicRendering / synchronization2；本引擎面向 Vulkan 1.3 桌面。");
    }

    // TD-02：graphics 阶段需要通过 storage buffer / storage image 消费
    // compute 产物（GPU culling / indirect draw 的同类路径）。
    if (!supportedFeatures.features.fragmentStoresAndAtomics)
        throw std::runtime_error("GPU 不支持 fragmentStoresAndAtomics；无法在 fragment shader 写 SSBO/storage image。");

    // ---- D-3 bindless 需要的 1.2 次特性 ----
    // 只启真正必需的，不做"看到支持就全开"——每多开一个都要在这里写清用途，
    // 否则将来没人知道某个特性为什么是开的。
    if (!supported12.descriptorIndexing ||
        !supported12.shaderSampledImageArrayNonUniformIndexing ||
        !supported12.descriptorBindingPartiallyBound ||
        !supported12.runtimeDescriptorArray ||
        !supported12.descriptorBindingVariableDescriptorCount ||
        !supported12.descriptorBindingSampledImageUpdateAfterBind)
    {
        throw std::runtime_error(
            "GPU 不支持 bindless 所需的 descriptorIndexing 次特性"
            "（descriptorIndexing / shaderSampledImageArrayNonUniformIndexing / "
            "descriptorBindingPartiallyBound / runtimeDescriptorArray / "
            "descriptorBindingVariableDescriptorCount / descriptorBindingSampledImageUpdateAfterBind）。");
    }

    // D-4：多帧在飞需要按数值等待某一帧的 GPU 工作完成。
    // timelineSemaphore 是 Vulkan 1.2 core feature；虽然实例 API 版本是 1.3，
    // 设备特性仍然必须显式启用。
    if (!supported12.timelineSemaphore)
        throw std::runtime_error("GPU 不支持 timelineSemaphore；D-4 多帧在飞无法安全实现。");

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.descriptorIndexing = VK_TRUE;
    // 同一个 draw call 里不同像素用不同索引去采数组 —— 这正是 bindless 的形态
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    // 数组尾部允许"未绑定"：材质槽位是复用池，不是所有槽位都随时有内容
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    // 数组长度在创建描述符集时才定（不是布局里写死），这样材质容量能调
    features12.runtimeDescriptorArray = VK_TRUE;
    // 允许用 VkDescriptorSetVariableDescriptorCountAllocateInfo 指定数组长度。
    // 注意这是一个**独立于 runtimeDescriptorArray 的开关** —— 少了它，
    // 布局里带 VARIABLE_DESCRIPTOR_COUNT_BIT 会被验证层直接拒绝。
    features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    // TD-04: allow material descriptor array entries to be refreshed while a
    // previously recorded graphics submission still has the set bound.
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    // D-4：用 timeline semaphore 追踪每个 frame slot 的 GPU 完成进度。
    features12.timelineSemaphore = VK_TRUE;

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.pNext = &features12;        // 1.3 -> 1.2 串起来

    // 注意：用 Features2 链传递特性时，pEnabledFeatures 必须为 null，
    // 否则两条路径同时启用特性是非法用法（VUID-VkDeviceCreateInfo-pNext-04748）。
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fragmentStoresAndAtomics = VK_TRUE;

    VkPhysicalDeviceFeatures2 enabledFeatures{};
    enabledFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enabledFeatures.pNext = &features13;
    enabledFeatures.features = deviceFeatures;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = nullptr;      // 特性全部走 pNext 链
    createInfo.pNext = &enabledFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) 
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } 
    else 
    {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) 
    {
        throw std::runtime_error("Failed to create logical device!");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    vkGetDeviceQueue(device, indices.computeFamily.value(), 0, &computeQueue);
    dedicatedComputeQueue = indices.computeFamily.value() != indices.graphicsFamily.value();

    std::cout << "[Device] Queue families: graphics=" << indices.graphicsFamily.value()
              << " present=" << indices.presentFamily.value()
              << " compute=" << indices.computeFamily.value()
              << (dedicatedComputeQueue ? " (dedicated async compute)" : " (fallback: graphics queue)")
              << "\n";

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    vmaCreateAllocator(&allocatorInfo, &vmaAllocator);
}

bool Device::isDeviceSuitable(VkPhysicalDevice device) 
{
    QueueFamilyIndices indices = findQueueFamilies(device);
    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) 
    {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool Device::checkDeviceExtensionSupport(VkPhysicalDevice device) 
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) 
    {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

QueueFamilyIndices Device::findQueueFamilies(VkPhysicalDevice device) 
{
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        const VkQueueFamilyProperties& queueFamily = queueFamilies[i];

        if ((queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.graphicsFamily)
            indices.graphicsFamily = i;

        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
        {
            // 优先独立 compute family；没有的话最后回退到 graphics family。
            if (!indices.computeFamily || !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                indices.computeFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport)
            indices.presentFamily = i;
    }

    // 所有常见桌面 GPU 的 graphics family 都同时支持 compute；这里仍显式回退，
    // 让 D-5 的调度代码只有一个路径，而不是在调用处到处判断 optional。
    if (!indices.computeFamily && indices.graphicsFamily)
        indices.computeFamily = indices.graphicsFamily;

    return indices;
}

std::vector<const char*> Device::getRequiredExtensions() 
{
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

bool Device::checkValidationLayerSupport() 
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) 
    {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) 
        {
            if (strcmp(layerName, layerProperties.layerName) == 0) 
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) return false;
    }
    return true;
}

Device::SwapChainSupportDetails Device::querySwapChainSupport(VkPhysicalDevice device) 
{
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) 
    {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) 
    {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

uint32_t Device::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) 
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) 
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) 
        {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

VkFormat Device::findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) 
{
    for (VkFormat format : candidates) 
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) return format;
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) return format;
    }
    throw std::runtime_error("failed to find supported format!");
}

VkFormat Device::findDepthFormat() 
{
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}