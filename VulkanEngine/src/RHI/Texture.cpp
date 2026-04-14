#include "Texture.h"
#include <stdexcept>
#include <iostream>
#include <stb_image.h> // 确保在某一个 cpp 文件中定义了 STB_IMAGE_IMPLEMENTATION

Texture::Texture(Device& device, const std::string& filepath, VkFormat format, VkCommandPool commandPool, bool isHDR)
    : vulkanDevice(device) 
{
    if (isHDR) 
    {
        loadHDR(filepath, commandPool);
    } 
    else 
    {
        loadLDR(filepath, format, commandPool);
    }
}

Texture::~Texture() 
{
    // RAII：对象销毁时自动清理 Vulkan 资源！
    if (sampler != VK_NULL_HANDLE) vkDestroySampler(vulkanDevice.getDevice(), sampler, nullptr);
    if (view != VK_NULL_HANDLE) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
    if (image != VK_NULL_HANDLE) vkDestroyImage(vulkanDevice.getDevice(), image, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);
}

Texture::Texture(Texture&& other) noexcept 
    : vulkanDevice(other.vulkanDevice), image(other.image), memory(other.memory), view(other.view), sampler(other.sampler) 
{
    other.image = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
    other.sampler = VK_NULL_HANDLE;
}

Texture& Texture::operator=(Texture&& other) noexcept 
{
    if (this != &other) 
    {
        // 先清理自己原有的资源
        if (sampler) vkDestroySampler(vulkanDevice.getDevice(), sampler, nullptr);
        if (view) vkDestroyImageView(vulkanDevice.getDevice(), view, nullptr);
        if (image) vkDestroyImage(vulkanDevice.getDevice(), image, nullptr);
        if (memory) vkFreeMemory(vulkanDevice.getDevice(), memory, nullptr);

        // 接管资源
        image = other.image;
        memory = other.memory;
        view = other.view;
        sampler = other.sampler;

        // 置空对方
        other.image = VK_NULL_HANDLE;
        other.memory = VK_NULL_HANDLE;
        other.view = VK_NULL_HANDLE;
        other.sampler = VK_NULL_HANDLE;
    }
    return *this;
}

void Texture::loadLDR(const std::string& filepath, VkFormat format, VkCommandPool commandPool) 
{
    // !!! 把你 FirstApp::createSingleTexture 里的代码原封不动复制到这里 !!!
    // 注意：把 outTexture.image 替换为 this->image，以此类推。
    int texWidth, texHeight, texChannels;

    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    VkDeviceSize imageSize = texWidth * texHeight * 4;



    if (!pixels)

    {

        throw std::runtime_error("[致命错误] 纹理加载失败！路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));

    }



    VkBuffer stagingBuffer;

    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bufferInfo{};

    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

    bufferInfo.size = imageSize;

    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer);



    VkMemoryRequirements memRequirements;

    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};

    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    allocInfo.allocationSize = memRequirements.size;

    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory);

    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingBufferMemory, 0);



    void* data;

    vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);

    memcpy(data, pixels, static_cast<size_t>(imageSize));

    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);

    stbi_image_free(pixels);



    VkImageCreateInfo imageInfo{};

    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    imageInfo.imageType = VK_IMAGE_TYPE_2D;

    imageInfo.extent.width = static_cast<uint32_t>(texWidth);

    imageInfo.extent.height = static_cast<uint32_t>(texHeight);

    imageInfo.extent.depth = 1;

    imageInfo.mipLevels = 1;

    imageInfo.arrayLayers = 1;

    imageInfo.format = format;

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(vulkanDevice.getDevice(), &imageInfo, nullptr, &this->image);



    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), this->image, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;

    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &this->memory);

    vkBindImageMemory(vulkanDevice.getDevice(), this->image, this->memory, 0);



    VkCommandBufferAllocateInfo allocInfoCmd{};

    allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    allocInfoCmd.commandPool = commandPool;

    allocInfoCmd.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;

    vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfoCmd, &commandBuffer);



    VkCommandBufferBeginInfo beginInfo{};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);



    VkImageMemoryBarrier barrier{};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = this->image;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseMipLevel = 0;

    barrier.subresourceRange.levelCount = 1;

    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = 0;

    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);



    VkBufferImageCopy region{};

    region.bufferOffset = 0;

    region.bufferRowLength = 0;

    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    region.imageSubresource.mipLevel = 0;

    region.imageSubresource.baseArrayLayer = 0;

    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};

    region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, this->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);



    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);



    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};

    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.commandBufferCount = 1;

    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    vkQueueWaitIdle(vulkanDevice.getGraphicsQueue());

    vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool, 1, &commandBuffer);



    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);

    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);



    VkImageViewCreateInfo viewInfo{};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    viewInfo.image = this->image;

    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    viewInfo.format = format;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    viewInfo.subresourceRange.baseMipLevel = 0;

    viewInfo.subresourceRange.levelCount = 1;

    viewInfo.subresourceRange.baseArrayLayer = 0;

    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(vulkanDevice.getDevice(), &viewInfo, nullptr, &this->view);



    VkSamplerCreateInfo samplerInfo{};

    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;

    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;

    samplerInfo.maxAnisotropy = 16.0f;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    samplerInfo.compareEnable = VK_FALSE;

    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &this->sampler);
}

void Texture::loadHDR(const std::string& filepath, VkCommandPool commandPool) 
{
    // !!! 把你 FirstApp::createHDRTexture 里的代码原封不动复制到这里 !!!
     int texWidth, texHeight, texChannels;

    float* pixels = stbi_loadf(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    VkDeviceSize imageSize = texWidth * texHeight * 4 * sizeof(float);



    if (!pixels)

    {

        throw std::runtime_error("[致命错误] HDR 全景图加载失败！\n路径: " + filepath + "\n原因: " + (stbi_failure_reason() ? stbi_failure_reason() : "未知"));

    }



    std::cout << "[Sentinel 通报] 成功萃取 HDR 浮点光能！大小: " << imageSize / 1024 / 1024 << " MB\n";



    VkBuffer stagingBuffer;

    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bufferInfo{};

    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;

    bufferInfo.size = imageSize; // 使用巨大的 imageSize

    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(vulkanDevice.getDevice(), &bufferInfo, nullptr, &stagingBuffer);



    VkMemoryRequirements memRequirements;

    vkGetBufferMemoryRequirements(vulkanDevice.getDevice(), stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};

    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    allocInfo.allocationSize = memRequirements.size;

    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &stagingBufferMemory);

    vkBindBufferMemory(vulkanDevice.getDevice(), stagingBuffer, stagingBufferMemory, 0);

    void* data;

    vkMapMemory(vulkanDevice.getDevice(), stagingBufferMemory, 0, imageSize, 0, &data);

    memcpy(data, pixels, static_cast<size_t>(imageSize));

    vkUnmapMemory(vulkanDevice.getDevice(), stagingBufferMemory);

    stbi_image_free(pixels);



    VkImageCreateInfo imageInfo{};

    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    imageInfo.imageType = VK_IMAGE_TYPE_2D;

    imageInfo.extent.width = static_cast<uint32_t>(texWidth);

    imageInfo.extent.height = static_cast<uint32_t>(texHeight);

    imageInfo.extent.depth = 1;

    imageInfo.mipLevels = 1;

    imageInfo.arrayLayers = 1;

    imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;

    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(vulkanDevice.getDevice(), &imageInfo, nullptr, &this->image);



    vkGetImageMemoryRequirements(vulkanDevice.getDevice(), this->image, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;

    allocInfo.memoryTypeIndex = vulkanDevice.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(vulkanDevice.getDevice(), &allocInfo, nullptr, &this->memory);

    vkBindImageMemory(vulkanDevice.getDevice(), this->image, this->memory, 0);



    VkCommandBufferAllocateInfo allocInfoCmd{};

    allocInfoCmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

    allocInfoCmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    allocInfoCmd.commandPool = commandPool;

    allocInfoCmd.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;

    vkAllocateCommandBuffers(vulkanDevice.getDevice(), &allocInfoCmd, &commandBuffer);



    VkCommandBufferBeginInfo beginInfo{};

    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);



    VkImageMemoryBarrier barrier{};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    barrier.image = this->image;

    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseMipLevel = 0;

    barrier.subresourceRange.levelCount = 1;

    barrier.subresourceRange.baseArrayLayer = 0;

    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = 0;

    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);



    VkBufferImageCopy region{};

    region.bufferOffset = 0;

    region.bufferRowLength = 0;

    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    region.imageSubresource.mipLevel = 0;

    region.imageSubresource.baseArrayLayer = 0;

    region.imageSubresource.layerCount = 1;

    region.imageOffset = {0, 0, 0};

    region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, this->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);



    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);



    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};

    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.commandBufferCount = 1;

    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vulkanDevice.getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);

    vkQueueWaitIdle(vulkanDevice.getGraphicsQueue());

    vkFreeCommandBuffers(vulkanDevice.getDevice(), commandPool, 1, &commandBuffer);



    vkDestroyBuffer(vulkanDevice.getDevice(), stagingBuffer, nullptr);

    vkFreeMemory(vulkanDevice.getDevice(), stagingBufferMemory, nullptr);



    VkImageViewCreateInfo viewInfo{};

    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    viewInfo.image = this->image;

    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

    viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;

    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    viewInfo.subresourceRange.baseMipLevel = 0;

    viewInfo.subresourceRange.levelCount = 1;

    viewInfo.subresourceRange.baseArrayLayer = 0;

    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(vulkanDevice.getDevice(), &viewInfo, nullptr, &this->view);



    VkSamplerCreateInfo samplerInfo{};

    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;

    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;

    samplerInfo.maxAnisotropy = 16.0f;



    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    samplerInfo.compareEnable = VK_FALSE;

    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    vkCreateSampler(vulkanDevice.getDevice(), &samplerInfo, nullptr, &this->sampler);
}