#pragma once

#include <chrono>

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>

#include "wsi/OsWindowingManager.hpp"

constexpr uint32_t NumFramesInFlight = 3;

class App
{
public:
    App();
    ~App();

    void run();

private:
    void drawFrame();
    
    void update();
private:
    OsWindowingManager windowing;
    std::unique_ptr<OsWindow> osWindow;

    glm::uvec2 resolution;
    bool useVsync;

    std::unique_ptr<etna::Window> vkWindow;
    std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

    etna::Image image;
    etna::Image texture_;
    
    etna::GraphicsPipeline texturePipeline;
    etna::GraphicsPipeline graphicsPipeline;
    etna::Sampler sampler;

    struct UniformParams {
        uint32_t resolutionX, resolutionY;
        float mouseX, mouseY;
        float time;
    };

    UniformParams params;
    std::chrono::steady_clock::time_point start;

    class FrameIter {
        std::array<etna::Buffer, NumFramesInFlight> buff;
        size_t iter = 0;
    public:
        etna::Buffer& next() {
            auto& res = buff[iter];
            iter = (iter + 1) % buff.size();
            return res;
        }
    };

    FrameIter frameIter;
};
