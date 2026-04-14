#pragma once
#include "IRenderPass.h"

class RenderPipeline 
{
public:
    void addPass(std::unique_ptr<IRenderPass> pass)
     {
        passes.push_back(std::move(pass));
    }

    // 提供给 FirstApp::render 使用
    const std::vector<std::unique_ptr<IRenderPass>>& getPasses() const 
    {
        return passes;
    }

    void initAll() 
    {
        for (auto& pass : passes) pass->init();
    }

    void resizeAll(VkExtent2D extent)
     {
        for (auto& pass : passes) pass->onResize(extent);
    }

private:
    std::vector<std::unique_ptr<IRenderPass>> passes;
};