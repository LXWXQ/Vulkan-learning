#pragma once
#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "RHI/Texture.h" 

enum class AlphaMode {
    OPAQUE_MODE,  // 不透明 (绝大部分物体)
    MASK_MODE,    // 遮罩裁剪 (树叶、铁丝网，需要 discard)
    BLEND_MODE    // 半透明混合 (玻璃，目前在延迟管线中暂不支持，留作前向管线扩展)
};

class Material {
public:
    std::string name;

    // --- PBR 基础属性 ---
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor{1.0f};
    float roughnessFactor{1.0f};
    float alphaCutoff{0.5f}; // 仅在 MASK 模式下生效
    AlphaMode alphaMode{AlphaMode::OPAQUE_MODE};

    // --- 贴图指针 (使用 shared_ptr 以便多网格复用) ---
    // 如果为空，则 Shader 应该使用上面的基础属性
    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> metallicRoughnessMap;
    std::shared_ptr<Texture> emissiveMap;
    std::shared_ptr<Texture> aoMap;

    // 为了兼容你的 Builder，提前准备好 DescriptorImageInfo
    // 如果没有贴图，你可以用一个全局的“白色 1x1 默认贴图”塞进去
    VkDescriptorImageInfo getAlbedoInfo() const 
    {
        if (albedoMap) return {albedoMap->getSampler(), albedoMap->getView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        return defaultWhiteTextureInfo; 
    }

    static VkDescriptorImageInfo defaultWhiteTextureInfo;
    static VkDescriptorImageInfo defaultNormalTextureInfo; 
};