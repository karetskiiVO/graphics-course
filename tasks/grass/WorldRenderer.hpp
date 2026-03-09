#pragma once

#include <etna/Image.hpp>
#include <glm/glm.hpp>

#include "wsi/Keyboard.hpp"
#include "FramePacket.hpp"
#include "GrassRenderer.hpp"

class WorldRenderer {
public:
    WorldRenderer ();

    void LoadShaders ();
    void AllocateResources (glm::uvec2 swapchainResolution);
    void SetupPipelines (vk::Format swapchainFormat);

    void DebugInput (const Keyboard& kb);
    void Update (const FramePacket& packet);
    void RenderWorld (vk::CommandBuffer cmdBuf, vk::Image targetImage, vk::ImageView targetImageView);

private:
    etna::Image mainViewDepth;
    glm::uvec2 resolution;

    glm::mat4 worldViewProj{1.0f};

    GrassRenderer grassRenderer;
};
