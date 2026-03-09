#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>

#include <glm/ext.hpp>

WorldRenderer::WorldRenderer () {}

void WorldRenderer::AllocateResources (glm::uvec2 swapchainResolution)
{
    resolution = swapchainResolution;

    mainViewDepth = etna::get_context().createImage(etna::Image::CreateInfo{
        .extent     = vk::Extent3D{resolution.x, resolution.y, 1},
        .name       = "main_view_depth",
        .format     = vk::Format::eD32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
    });

    grassRenderer.AllocateResources();
}

void WorldRenderer::LoadShaders ()
{
    grassRenderer.LoadShaders();
}

void WorldRenderer::SetupPipelines (vk::Format swapchainFormat)
{
    grassRenderer.SetupPipelines(swapchainFormat, vk::Format::eD32Sfloat);
}

void WorldRenderer::DebugInput (const Keyboard&) {}

void WorldRenderer::Update (const FramePacket& packet)
{
    ZoneScoped;

    const float aspect = static_cast<float>(resolution.x) / static_cast<float>(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();

    grassRenderer.Update(worldViewProj, packet.mainCam.position, packet.currentTime);
}

void WorldRenderer::RenderWorld (
    vk::CommandBuffer cmdBuf,
    vk::Image         targetImage,
    vk::ImageView     targetImageView)
{
    ETNA_PROFILE_GPU(cmdBuf, renderWorld);

    // Phase 1: GPU compute – place grass blades around the camera.
    // Must happen outside of a render pass.
    {
        ETNA_PROFILE_GPU(cmdBuf, grassPlacementPass);
        grassRenderer.DispatchPlacement(cmdBuf);
    }

    // Phase 2: Forward render pass – draw the grass field.
    {
        ETNA_PROFILE_GPU(cmdBuf, grassForwardPass);

        etna::RenderTargetState renderTargets(
            cmdBuf,
            {{0, 0}, {resolution.x, resolution.y}},
            {{.image = targetImage, .view = targetImageView}},
            {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

        grassRenderer.DrawBlades(cmdBuf);
    }
}
