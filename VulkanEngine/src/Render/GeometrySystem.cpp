#include "GeometrySystem.h"
#include "Core/DescriptorManager.h"
#include "RHI/CommandUtils.h"
#include "RHI/GBuffer.h"
#include "AssetPaths.h"
#include <iostream>
#include <stdexcept>

GeometrySystem::GeometrySystem(Device& device, VkFormat depthFormat,
                               VkDescriptorSetLayout globalSetLayout,
                               VkDescriptorSetLayout materialSetLayout)
    : vulkanDevice(device), depthFormat(depthFormat)
{
    createPipelineLayout(globalSetLayout, materialSetLayout);
    createPipelines();
}

GeometrySystem::~GeometrySystem()
{
    vkDestroyPipelineLayout(vulkanDevice.getDevice(), pipelineLayout, nullptr);
}

void GeometrySystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout,
                                          VkDescriptorSetLayout materialSetLayout)
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(SimplePushConstantData);

    std::vector<VkDescriptorSetLayout> descriptorSetLayouts{ globalSetLayout, materialSetLayout };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkanDevice.getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("GeometrySystem: pipeline layout failed!");
}

void GeometrySystem::createPipelines()
{
    PipelineConfigInfo config{};
    Pipeline::defaultPipelineConfigInfo(config, WIDTH, HEIGHT);

    // 动态渲染：附件格式必须与渲染图里 writeColor 的顺序完全一致
    config.colorAttachmentFormats = {
        GBufferFormat::Position, GBufferFormat::Normal, GBufferFormat::Albedo, GBufferFormat::Pbr
    };
    config.depthAttachmentFormat = depthFormat;
    config.pipelineLayout = pipelineLayout;

    // 4 个颜色附件，混合全部关闭
    VkPipelineColorBlendAttachmentState defaultAttachment = config.colorBlendAttachments[0];
    config.colorBlendAttachments.assign(4, defaultAttachment);
    config.colorBlendInfo.attachmentCount = 4;
    config.colorBlendInfo.pAttachments = config.colorBlendAttachments.data();

    geometryPipeline = std::make_unique<Pipeline>(vulkanDevice,
        SENTINEL_SHADER_DIR "mrt.vert.spv", SENTINEL_SHADER_DIR "mrt.frag.spv", config);
    gridPipeline = std::make_unique<Pipeline>(vulkanDevice,
        SENTINEL_SHADER_DIR "grid_mrt.vert.spv", SENTINEL_SHADER_DIR "grid_mrt.frag.spv", config);

    // 天空盒：不写深度、深度比较改成 LESS_OR_EQUAL（配合 gl_Position = pos.xyww）
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyboxPipeline = std::make_unique<Pipeline>(vulkanDevice,
        SENTINEL_SHADER_DIR "skybox_mrt.vert.spv", SENTINEL_SHADER_DIR "skybox_mrt.frag.spv", config);
}

void GeometrySystem::render(VkCommandBuffer commandBuffer, FrameInfo& frameInfo)
{
    DescriptorManager& descMgr = *frameInfo.descriptorManager;

    // -----------------------------------------------------------------------
    // D-3 bindless：**整帧只绑一次 set 1**。
    //
    // 以前是"每个子网格切材质就 vkCmdBindDescriptorSets 一次"—— Sponza 有
    // 103 个子网格，于是 104 次 draw 伴随多达 104 次描述符集绑定。
    // 现在材质纹理在一个数组里，下标走 push constant，绑定与绘制彻底解耦。
    // -----------------------------------------------------------------------
    VkDescriptorSet sets[] = { descMgr.getGlobalSet(), descMgr.getMaterialSet() };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 2, sets, 0, nullptr);
    frameInfo.telemetry.descriptorBinds += 1;

    // 没材质时用哪个下标：数组尾部允许未绑定（descriptorBindingPartiallyBound），
    // 但"没绑定的内容"采出来是未定义值。所以退化路径不指向数组，
    // 而是让着色器走 materialValid = 0 的分支（直接输出顶点色 / 默认 PBR）。
    const uint32_t fallbackMatId = 0;

    for (auto& obj : frameInfo.gameObjects)
    {
        if (obj.model == nullptr) continue;

        SimplePushConstantData push{};
        // 法线必须用「逆转置矩阵」变换，非均匀缩放下直接用 modelMatrix 会错
        const glm::mat4 modelMatrix = obj.transform.mat4();
        push.modelMatrix = modelMatrix;
        push.normalMatrix = glm::transpose(glm::inverse(modelMatrix));

        // 天空盒 / 程序化网格：着色器不采材质数组。这个标记必须显式传下去，
        // 因为数组里对应下标可能根本没绑过内容。
        const bool objectUsesMaterial = !obj.isSkybox && !obj.isGrid;
        push.materialValid = objectUsesMaterial ? 1u : 0u;
        push.materialId = fallbackMatId;

        auto* pipe = &geometryPipeline;
        if (obj.isSkybox)    pipe = &skyboxPipeline;
        else if (obj.isGrid) pipe = &gridPipeline;

        (*pipe)->bind(commandBuffer);

        obj.model->bind(commandBuffer);

        for (const SubMesh& subMesh : obj.model->getSubMeshes())
        {
            // 子网格自带材质就用它（glTF 走这条）；否则回退到 GameObject 的材质
            uint32_t slot = (subMesh.materialSlot != kInvalidMaterialSlot)
                ? subMesh.materialSlot
                : (obj.material ? obj.material->descriptorSetIndex : obj.materialSetIndex);

            if (objectUsesMaterial && slot == kInvalidMaterialSlot)
            {
                // 以前这里是 continue（静默少画一个子网格）。但那会让"材质装配漏了"
                // 表现成"模型缺了一块"，很难和建模问题区分。现在退化成
                // materialValid=0 的默认着色路径，至少还能看见几何体本身。
                std::cerr << "[GeometrySystem] 物体 " << obj.getId()
                          << " 的子网格没有有效材质槽位，退化为默认着色。\n";
                slot = fallbackMatId;
                push.materialValid = 0u;
            }
            else if (slot != kInvalidMaterialSlot)
            {
                push.materialValid = objectUsesMaterial ? 1u : 0u;
            }

            // 切材质只是改一个整数 —— 不再重绑描述符集
            push.materialId = (slot == kInvalidMaterialSlot) ? fallbackMatId : slot;
            vkCmdPushConstants(commandBuffer, pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SimplePushConstantData), &push);

            obj.model->drawSubMesh(commandBuffer, subMesh);

            frameInfo.telemetry.drawCalls += 1;
            frameInfo.telemetry.triangles += subMesh.indexCount / 3;
            frameInfo.telemetry.vertices += subMesh.indexCount;
        }
    }
}