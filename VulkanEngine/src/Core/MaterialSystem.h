#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include "Material.h"
#include "RHI/Device.h"
#include "Core/DescriptorManager.h"

class MaterialSystem
{
public:
	void initDefaultTextures(Device& device, VkCommandPool commandPool);
	void buildMaterialDescriptor(std::shared_ptr<Material> material, DescriptorManager& descMgr);
	void cleanup();

	std::shared_ptr<Material> getOrCreateMaterial(const std::string& name);
	std::shared_ptr<Texture> loadTexture(const std::string& filepath, Device& device, VkCommandPool commandPool);
	std::shared_ptr<Texture> getTexture(const std::string& filepath) const;

private:
	std::unordered_map<std::string, std::shared_ptr<Material>> materials;
	std::unordered_map<std::string, std::shared_ptr<Texture>> textureCache;

	std::shared_ptr<Texture> defaultWhiteTex;
	std::shared_ptr<Texture> defaultBlackTex;
	std::shared_ptr<Texture> defaultNormalTex;
};
