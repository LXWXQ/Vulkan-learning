#include "ImGuiSystem.hpp"
#include <stdexcept>

ImGuiSystem::ImGuiSystem(GLFWwindow* window, VulkanDevice& device, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
    : vulkanDevice(device) 
{
    // 1. 创建 ImGui 专属的 Descriptor Pool
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(device.getDevice(), &pool_info, nullptr, &imguiPool);

    // 2. 初始化 ImGui 核心上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // 3. 初始化平台后端
    ImGui_ImplGlfw_InitForVulkan(window, true);
    
    // 4. 初始化 Vulkan 后端 (✨ 适配 ImGui 1.91+ 最新 API ✨)
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = device.getInstance();
    init_info.PhysicalDevice = device.getPhysicalDevice();
    init_info.Device = device.getDevice();
    init_info.QueueFamily = device.findPhysicalQueueFamilies().graphicsFamily.value();
    init_info.Queue = device.getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 1;
    ImGui_ImplVulkan_Init(&init_info);
}

ImGuiSystem::~ImGuiSystem() {
   ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
     vkDestroyDescriptorPool(vulkanDevice.getDevice(), imguiPool, nullptr);
}

void ImGuiSystem::newFrame() 
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiSystem::render(VkCommandBuffer commandBuffer, VulkanGameObject& cameraObj, float dt) 
{
    // ========================================================
    // 工业级引擎调试面板
    // ========================================================
    ImGui::Begin("Sentinel Engine Core");
    
    // 核心性能指标
    ImGui::Text("Performance");
    ImGui::Text("FPS: %.1f (%.2f ms/frame)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Separator();

    // 摄像机遥测数据 (只读/辅助微调)
    ImGui::Text("Camera Telemetry");
    ImGui::DragFloat3("Position (X,Y,Z)", &cameraObj.transform.translation.x, 1.0f);
    
    // 把弧度转成角度显示，更符合人类直觉
    glm::vec3 eulerAngles = glm::degrees(cameraObj.transform.rotation);
    if (ImGui::DragFloat3("Rotation (P,Y,R)", &eulerAngles.x, 1.0f)) {
        cameraObj.transform.rotation = glm::radians(eulerAngles);
    }

    ImGui::Separator();
    ImGui::Text("Tips:");
    ImGui::Text("Hold [Right Mouse Button] to look around.");
    ImGui::Text("Use [W/A/S/D/Q/E] to move.");

    ImGui::End();
    // ========================================================

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}