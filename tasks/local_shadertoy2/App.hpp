#pragma once

#include <chrono>
#include <map>

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>

#include "wsi/OsWindowingManager.hpp"


class App {
public:
    App();
    ~App();

    void Run();

private:
    void DrawFrame();
    void Update();
    void DrawProceduralTexture(
        vk::CommandBuffer cmd, const etna::Image& target, const etna::GraphicsPipeline& pipeline);
    bool Present(vk::Semaphore renderingDone, vk::ImageView backbufferView);

private:
    OsWindowingManager windowing;
    std::unique_ptr<OsWindow> osWindow;

    glm::uvec2 resolution;
    bool useVsync;

    std::unique_ptr<etna::Window> vkWindow;
    std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
    std::map<vk::ImageView, vk::UniqueSemaphore> presentSemaphores;

    etna::Sampler sampler;
    etna::Image bufferA;
    etna::Image bufferB;
    etna::GraphicsPipeline bufferAPipeline;
    etna::GraphicsPipeline bufferBPipeline;
    etna::GraphicsPipeline pipeline;

    struct PushConstants {
        uint32_t resolutionX, resolutionY;
        float mouseX, mouseY;
        float time;
    };

    PushConstants pushConstants{};
    std::chrono::steady_clock::time_point start;
};
