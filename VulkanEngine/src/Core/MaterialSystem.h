#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <iostream>
#include <glm/glm.hpp>

#include "Material.h"
#include "RHI/Device.h"

class MaterialSystem 
{
public:

    void initDefaultTextures(Device& device, VkCommandPool commandPool) 
    {
        // 战术提示：这里你应该调用你的 Texture 构造函数，传入 1x1 像素的内存数据
        // 以下为伪代码，你需要根据你的 Texture 类支持的内存加载方式来实现：
        
        // unsigned char whitePixel[4] = {255, 255, 255, 255};
        // defaultWhiteTex = std::make_shared<Texture>(device, whitePixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, commandPool);
        
        // unsigned char blackPixel[4] = {0, 0, 0, 255};
        // defaultBlackTex = std::make_shared<Texture>(device, blackPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, commandPool);
        
        // unsigned char flatNormalPixel[4] = {128, 128, 255, 255}; // 切线空间中的 Z 轴朝上
        // defaultNormalTex = std::make_shared<Texture>(device, flatNormalPixel, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, commandPool);

        // 🚨 假设你已经成功创建了上述贴图，现在把它们的 info 塞给 Material 的静态变量
        // Material::defaultWhiteTextureInfo = {defaultWhiteTex->getSampler(), defaultWhiteTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // Material::defaultBlackTextureInfo = {defaultBlackTex->getSampler(), defaultBlackTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        // Material::defaultNormalTextureInfo = {defaultNormalTex->getSampler(), defaultNormalTex->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        
        std::cout << "[MaterialSystem] 默认替身贴图初始化完毕。" << std::endl;
    }

    // 获取或创建材质
    std::shared_ptr<Material> getOrCreateMaterial(const std::string& name) 
    {
        if (materials.find(name) != materials.end()) 
        {
            return materials[name];
        }
        auto mat = std::make_shared<Material>();
        mat->name = name;
        materials[name] = mat;
        return mat;
    }

    // 缓存已加载的纹理，防止路径相同的纹理被重复加载进显存
    std::shared_ptr<Texture> loadTexture(const std::string& filepath, Device& device, VkCommandPool commandPool)
    {
        if (textureCache.find(filepath) != textureCache.end()) 
        {
            return textureCache[filepath]; // 命中缓存，直接返回！
        }
        
        std::cout << "[MaterialSystem] 加载新纹理: " << filepath << std::endl;
        auto tex = std::make_shared<Texture>(device, filepath, VK_FORMAT_R8G8B8A8_UNORM, commandPool);
        textureCache[filepath] = tex;
        return tex;
    }

    // 引擎关闭时清理
    void cleanup() 
    {
        materials.clear();
        textureCache.clear();
        defaultWhiteTex.reset();
        defaultBlackTex.reset();
        defaultNormalTex.reset();
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Material>> materials;
    std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

    // 持有默认替身贴图的生命周期
    std::shared_ptr<Texture> defaultWhiteTex;
    std::shared_ptr<Texture> defaultBlackTex;
    std::shared_ptr<Texture> defaultNormalTex;
};