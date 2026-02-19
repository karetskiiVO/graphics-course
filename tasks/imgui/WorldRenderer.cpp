#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>

#include <glm/ext.hpp>

#include "TerrainGenerator.hpp"

WorldRenderer::WorldRenderer () { terrainGen = std::make_unique<TerrainGenerator>(); }

void WorldRenderer::allocateResources (glm::uvec2 swapchain_resolution) {
    resolution = swapchain_resolution;

    auto& ctx = etna::get_context();

    mainViewDepth = ctx.createImage(
        etna::Image::CreateInfo{
            .extent = vk::Extent3D{resolution.x, resolution.y, 1},
            .name = "main_view_depth",
            .format = vk::Format::eD32Sfloat,
            .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
        }
    );

    heightMap = terrainGen->GenerateHeightMap(4096, 4096, 6);
    heightMapSampler = etna::Sampler(
        etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eClampToEdge, .name = "height_map_sampler",}
    );
}

void WorldRenderer::loadShaders () {
    terrainShader.Create(
        "terrain",
        {
            IMGUI_SHADERS_ROOT "terrain.frag.spv",
            IMGUI_SHADERS_ROOT "terrain.vert.spv",
            IMGUI_SHADERS_ROOT "terrain.tesc.spv",
            IMGUI_SHADERS_ROOT "terrain.tese.spv"
        }
    );
}

void WorldRenderer::setupPipelines (vk::Format swapchain_format) {
    auto& pipelineManager = etna::get_context().getPipelineManager();

    terrainPipeline = pipelineManager.createGraphicsPipeline(
        terrainShader.Name().c_str(),
        etna::GraphicsPipeline::CreateInfo{
            .vertexShaderInput = {},
            .inputAssemblyConfig = vk::PipelineInputAssemblyStateCreateInfo{
                .topology = vk::PrimitiveTopology::ePatchList,
            },
            .tessellationConfig = vk::PipelineTessellationStateCreateInfo{
                .patchControlPoints = 4,
            },
            .rasterizationConfig = vk::PipelineRasterizationStateCreateInfo{
                .polygonMode = wireframeMode ? vk::PolygonMode::eLine : vk::PolygonMode::eFill,
                .cullMode = vk::CullModeFlagBits::eBack,
                .frontFace = vk::FrontFace::eCounterClockwise,
                .lineWidth = 1.f,
            },
            .fragmentShaderOutput = {
                .colorAttachmentFormats = {swapchain_format},
                .depthAttachmentFormat = vk::Format::eD32Sfloat,
            },
        }
    );
}

void WorldRenderer::debugInput (const Keyboard& kb) {
    if (kb[KeyboardKey::kT] == ButtonState::Falling) {
        wireframeMode = !wireframeMode;
        spdlog::info("Wireframe mode: {}", wireframeMode ? "ON" : "OFF");
    }

    if (is_held_down(kb[KeyboardKey::kUp])) tessellationFactor = std::min(128.0f, tessellationFactor + 1.0f);
    if (is_held_down(kb[KeyboardKey::kDown])) tessellationFactor = std::max(1.0f, tessellationFactor - 1.0f);

    if (is_held_down(kb[KeyboardKey::kLeft])) heightScale = std::max(0.0f, heightScale - 0.5f);
    if (is_held_down(kb[KeyboardKey::kRight])) heightScale = heightScale + 0.5f;
}

void WorldRenderer::update (const FramePacket& packet) {
    ZoneScoped;

    cameraPos = packet.mainCam.position;

    const float aspect = float(resolution.x) / float(resolution.y);
    viewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
}

void WorldRenderer::renderTerrain (vk::CommandBuffer cmd_buf) {
    auto terrainShaderInfo = etna::get_shader_program(terrainShader.Name().c_str());

    auto descriptorSet = etna::create_descriptor_set(
        terrainShaderInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{0, heightMap.genBinding(heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}}
    );

    cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        terrainPipeline.getVkPipelineLayout(),
        0,
        {descriptorSet.getVkSet()},
        {}
    );

    float halfTerrainSize = (gridSize * chunkSize) / 2.0f;
    for (int y = 0; y < gridSize; ++y) {
        for (int x = 0; x < gridSize; ++x) {
            pushConstants.viewProj = viewProj;
            pushConstants.cameraPos = cameraPos;
            pushConstants.heightScale = heightScale;
            pushConstants.chunkOffset = glm::vec2(x * chunkSize - halfTerrainSize, y * chunkSize - halfTerrainSize);
            pushConstants.chunkSize = chunkSize;
            pushConstants.tessellationFactor = tessellationFactor;

            cmd_buf.pushConstants<PushConstants>(
                terrainPipeline.getVkPipelineLayout(),
                vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
                vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
                0,
                {pushConstants}
            );

            cmd_buf.draw(4, 1, 0, 0);
        }
    }
}

void WorldRenderer::renderWorld (vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view) {
    ETNA_PROFILE_GPU(cmd_buf, renderWorld);

    {
        ETNA_PROFILE_GPU(cmd_buf, renderTerrain);

        etna::RenderTargetState renderTargets(
            cmd_buf,
            {{0, 0}, {resolution.x, resolution.y}},
            {{.image = target_image, .view = target_image_view}},
            {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}
        );

        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());
        renderTerrain(cmd_buf);
    }
}
