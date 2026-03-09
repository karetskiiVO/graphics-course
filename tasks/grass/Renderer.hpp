#pragma once

#include <etna/GlobalContext.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <glm/glm.hpp>
#include <function2/function2.hpp>

#include "wsi/Keyboard.hpp"
#include "FramePacket.hpp"
#include "WorldRenderer.hpp"

using ResolutionProvider = fu2::unique_function<glm::uvec2() const>;

class Renderer
{
public:
    explicit Renderer (glm::uvec2 resolution);
    ~Renderer ();

    void InitVulkan (std::span<const char*> instanceExtensions);
    void InitFrameDelivery (vk::UniqueSurfaceKHR surface, ResolutionProvider provider);

    void DebugInput (const Keyboard& kb);
    void Update (const FramePacket& packet);
    void DrawFrame ();

private:
    ResolutionProvider            resolutionProvider;
    std::unique_ptr<etna::Window> window;
    std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

    glm::uvec2 resolution;
    bool useVsync = true;

    std::unique_ptr<WorldRenderer> worldRenderer;
};
