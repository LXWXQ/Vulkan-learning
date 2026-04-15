#include "ImGuiSystem.h"
#include <stdexcept>

ImGuiSystem::ImGuiSystem(GLFWwindow* window, Device& device, VkRenderPass renderPass, uint32_t imageCount, VkCommandPool commandPool)
    : vulkanDevice(device) 
{
    VkDescriptorPoolSize pool_sizes[] = 
    {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1;
    pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    vkCreateDescriptorPool(device.getDevice(), &pool_info, nullptr, &imguiPool);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForVulkan(window, true);

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
    init_info.PipelineInfoMain.Subpass = 0;
    ImGui_ImplVulkan_Init(&init_info);
}

ImGuiSystem::~ImGuiSystem() 
{
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

void ImGuiSystem::render(VkCommandBuffer commandBuffer, GameObject& cameraObj, float dt, EngineSettings& settings, RenderTelemetry& telemetry) 
{
    ImGui::Begin("Sentinel Engine Core");

    // ==========================================
    // 1. 核心性能遥测 (Telemetry)
    // ==========================================
    if (ImGui::CollapsingHeader("Performance & Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) 
    {
        ImGui::Text("FPS: %.1f (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Text("Draw Calls : %u", telemetry.drawCalls);
        ImGui::Text("Triangles  : %u", telemetry.triangles);
        ImGui::Text("Vertices   : %u", telemetry.vertices);
    }

    // ==========================================
    // 2. SSAO 控制面板
    // ==========================================
    if (ImGui::CollapsingHeader("SSAO Settings", ImGuiTreeNodeFlags_DefaultOpen)) 
    {
        ImGui::SliderFloat("Radius", &settings.ssaoRadius, 0.1f, 3.0f, "%.2f");
        ImGui::SliderFloat("Bias", &settings.ssaoBias, 0.0f, 0.2f, "%.3f");
        ImGui::SliderInt("Samples", &settings.ssaoKernelSize, 4, 64);
    }

    // ==========================================
    // 3. PBR 光照控制面板
    // ==========================================
    if (ImGui::CollapsingHeader("PBR Lighting", ImGuiTreeNodeFlags_DefaultOpen)) 
    {
        ImGui::SliderFloat("Exposure", &settings.exposure, 0.1f, 5.0f);
        
        // 用一个圆球形 UI 控制光照方向非常直观
        ImGui::SliderFloat3("Sun Direction", &settings.directionalLightDir.x, -1.0f, 1.0f);
        ImGui::ColorEdit3("Sun Color", &settings.directionalLightColor.x);
    }

    // ==========================================
    // 4. 相机遥测
    // ==========================================
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) 
    {
        ImGui::DragFloat3("Position", &cameraObj.transform.translation.x, 0.1f);
        glm::vec3 eulerAngles = glm::degrees(cameraObj.transform.rotation);
        if (ImGui::DragFloat3("Rotation", &eulerAngles.x, 1.0f)) {
            cameraObj.transform.rotation = glm::radians(eulerAngles);
        }
    }

    ImGui::End();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}