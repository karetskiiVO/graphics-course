module;

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>
#include <memory>

#include "wsi/Keyboard.hpp"
#include <function2/function2.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/ShaderProgram.hpp>

#include <glm/ext.hpp>
#include <imgui.h>

export module TerrainRenderer;
import Engine;
import Graphics;
import TerrainGenerator;

export class TerrainRendererComponent : public World::Entity::Component {
public:
    void Awake() override {
        terrainGen = std::make_unique<TerrainGenerator>();
    }

    void Start() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        resolution = system->resolution;

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
        splatMap = terrainGen->GenerateSplatMap(4096, 4096, 6);
        detailTextures = terrainGen->GenerateDetailTextures(512);
        detailNormals = terrainGen->GenerateDetailNormalMaps(512);

        heightMapSampler = etna::Sampler(
            etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eClampToEdge, .name = "height_map_sampler",}
        );
        splatMapSampler = etna::Sampler(
            etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eClampToEdge, .name = "splat_map_sampler",}
        );
        detailSampler = etna::Sampler(
            etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eRepeat, .name = "detail_sampler",}
        );

        for (uint32_t i = 0; i < clipmapCascadeCount; ++i) {
            auto cascadeImage = ctx.createImage(etna::Image::CreateInfo{
                .extent = vk::Extent3D{clipmapResolution, clipmapResolution, 1},
                .name = std::string("clipmap_cascade_") + std::to_string(i),
                .format = vk::Format::eR8G8B8A8Unorm,
                .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            });
            clipmapCascades.push_back(std::move(cascadeImage));
        }
        clipmapSampler = etna::Sampler(
            etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eClampToEdge, .name = "clipmap_sampler",}
        );

        auto& pipelineManager = ctx.getPipelineManager();

        etna::create_program("terrain", {
            SUPERTASK_SHADERS_ROOT "/terrain.frag.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.vert.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.tesc.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.tese.spv"
        });

        etna::create_program("clipmap_splat", {
            SUPERTASK_SHADERS_ROOT "/clipmap_splat.frag.spv",
            SUPERTASK_SHADERS_ROOT "/clipmap_splat.vert.spv",
        });

        terrainPipeline = pipelineManager.createGraphicsPipeline(
            "terrain",
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
                    .colorAttachmentFormats = {system->etnaWindow->getCurrentFormat()},
                    .depthAttachmentFormat = vk::Format::eD32Sfloat,
                },
            }
        );

        clipmapSplatPipeline = pipelineManager.createGraphicsPipeline(
            "clipmap_splat",
            etna::GraphicsPipeline::CreateInfo{
                .vertexShaderInput = {},
                .inputAssemblyConfig = vk::PipelineInputAssemblyStateCreateInfo{
                    .topology = vk::PrimitiveTopology::eTriangleList,
                },
                .rasterizationConfig = vk::PipelineRasterizationStateCreateInfo{
                    .polygonMode = vk::PolygonMode::eFill,
                    .cullMode = vk::CullModeFlagBits::eNone,
                    .frontFace = vk::FrontFace::eCounterClockwise,
                    .lineWidth = 1.f,
                },
                .fragmentShaderOutput = {
                    .colorAttachmentFormats = {vk::Format::eR8G8B8A8Unorm},
                },
            }
        );

        lastClipmapUpdatePos = glm::vec3(1e10f);
    }

    void Update() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        auto kb = system->mainWindow->keyboard;

        if (kb[KeyboardKey::kT] == ButtonState::Falling) {
            wireframeMode = !wireframeMode;
            spdlog::info("Wireframe mode: {}", wireframeMode ? "ON" : "OFF");
            auto& pipelineManager = etna::get_context().getPipelineManager();
            terrainPipeline = pipelineManager.createGraphicsPipeline(
                "terrain",
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
                        .colorAttachmentFormats = {system->etnaWindow->getCurrentFormat()},
                        .depthAttachmentFormat = vk::Format::eD32Sfloat,
                    },
                }
            );
        }

        if (is_held_down(kb[KeyboardKey::kUp])) tessellationFactor = std::min(128.0f, tessellationFactor + 1.0f);
        if (is_held_down(kb[KeyboardKey::kDown])) tessellationFactor = std::max(1.0f, tessellationFactor - 1.0f);

        if (is_held_down(kb[KeyboardKey::kLeft])) heightScale = std::max(0.0f, heightScale - 0.5f);
        if (is_held_down(kb[KeyboardKey::kRight])) heightScale = heightScale + 0.5f;
    }

    void updateClipmapCascades(vk::CommandBuffer cmdBuf) {
        auto dist = glm::length(cameraPos - lastClipmapUpdatePos);
        if (dist < clipmapUpdateThreshold && !forceClipmapUpdate) return;

        lastClipmapUpdatePos = cameraPos;
        forceClipmapUpdate = false;

        auto clipmapShaderInfo = etna::get_shader_program("clipmap_splat");

        for (uint32_t cascade = 0; cascade < clipmapCascadeCount; ++cascade) {
            auto cascadeWorldSize = clipmapBaseSize * std::pow(2.0f, static_cast<float>(cascade));

            {
                auto descriptorSet = etna::create_descriptor_set(
                    clipmapShaderInfo.getDescriptorLayoutId(0),
                    cmdBuf,
                    {
                        etna::Binding{0, splatMap.genBinding(splatMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                        etna::Binding{1, detailTextures.genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {.type = vk::ImageViewType::e2DArray})},
                        etna::Binding{2, detailNormals.genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {.type = vk::ImageViewType::e2DArray})},
                        etna::Binding{3, heightMap.genBinding(heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    }
                );

                etna::flush_barriers(cmdBuf);

                etna::RenderTargetState renderTargets(
                    cmdBuf,
                    {{0, 0}, {clipmapResolution, clipmapResolution}},
                    {{.image = clipmapCascades[cascade].get(), .view = clipmapCascades[cascade].getView({})}},
                    {}
                );

                cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, clipmapSplatPipeline.getVkPipeline());

                cmdBuf.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    clipmapSplatPipeline.getVkPipelineLayout(),
                    0,
                    {descriptorSet.getVkSet()},
                    {}
                );

                struct ClipmapPushConstants {
                    glm::vec2 centerWorldPos;
                    float cascadeWorldSize;
                    float terrainSize;
                    float detailTiling;
                    float heightScale;
                    float padding1;
                    float padding2;
                } clipmapPC;

                auto terrainSize = chunkSize * gridSize;
                clipmapPC.centerWorldPos = glm::vec2(cameraPos.x, cameraPos.z);
                clipmapPC.cascadeWorldSize = cascadeWorldSize;
                clipmapPC.terrainSize = terrainSize;
                clipmapPC.detailTiling = detailTiling;
                clipmapPC.heightScale = heightScale;
                clipmapPC.padding1 = 0.0f;
                clipmapPC.padding2 = 0.0f;

                cmdBuf.pushConstants<ClipmapPushConstants>(
                    clipmapSplatPipeline.getVkPipelineLayout(),
                    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
                    0,
                    {clipmapPC}
                );

                cmdBuf.draw(3, 1, 0, 0);
            }
        }
    }

    void PreRender() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        auto cmd_buf = system->GetCurrentCmdBuf();
        if (!cmd_buf) return;
        auto target_image = system->GetTargetImage();
        auto target_image_view = system->GetTargetImageView();

        ETNA_PROFILE_GPU(cmd_buf, renderWorld);

        auto cameraEntity = GetOwner().GetWorld()->GetEntity("MainCamera");
        if (!cameraEntity) return;
        auto cameraComponent = cameraEntity->GetComponent<CameraComponent>();
        if (!cameraComponent) return;

        cameraPos = cameraComponent->mainCam.position;
        auto aspect = float(resolution.x) / float(resolution.y);
        viewProj = cameraComponent->mainCam.projTm(aspect) * cameraComponent->mainCam.viewTm();

        if (useClipmap) updateClipmapCascades(cmd_buf);

        {
            ETNA_PROFILE_GPU(cmd_buf, renderTerrain);

            auto terrainShaderInfo = etna::get_shader_program("terrain");

            std::vector<etna::Binding> bindings;
            bindings.push_back(etna::Binding{0, heightMap.genBinding(heightMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
            bindings.push_back(etna::Binding{1, splatMap.genBinding(splatMapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
            bindings.push_back(etna::Binding{2, detailTextures.genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {.type = vk::ImageViewType::e2DArray})});
            bindings.push_back(etna::Binding{3, detailNormals.genBinding(detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal, {.type = vk::ImageViewType::e2DArray})});

            for (uint32_t i = 0; i < clipmapCascadeCount; ++i) {
                bindings.push_back(etna::Binding{4 + i, clipmapCascades[i].genBinding(clipmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
            }

            auto descriptorSet = etna::create_descriptor_set(
                terrainShaderInfo.getDescriptorLayoutId(0),
                cmd_buf,
                bindings
            );

            etna::flush_barriers(cmd_buf);

            etna::RenderTargetState renderTargets(
                cmd_buf,
                {{0, 0}, {resolution.x, resolution.y}},
                {{.image = target_image, .view = target_image_view}},
                {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}
            );

            cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());

            cmd_buf.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                terrainPipeline.getVkPipelineLayout(),
                0,
                {descriptorSet.getVkSet()},
                {}
            );

            auto halfTerrainSize = (gridSize * chunkSize) / 2.0f;
            for (int y = 0; y < gridSize; ++y) {
                for (int x = 0; x < gridSize; ++x) {
                    pushConstants.viewProj = viewProj;
                    pushConstants.cameraPos = cameraPos;
                    pushConstants.heightScale = heightScale;
                    pushConstants.chunkOffset = glm::vec2(x * chunkSize - halfTerrainSize, y * chunkSize - halfTerrainSize);
                    pushConstants.chunkSize = chunkSize;
                    pushConstants.tessellationFactor = tessellationFactor;
                    pushConstants.terrainSize = gridSize * chunkSize;
                    pushConstants.detailTiling = detailTiling;
                    pushConstants.useClipmap = useClipmap ? 1.0f : 0.0f;
                    pushConstants.clipmapBaseSize = clipmapBaseSize;
                    pushConstants.clipmapCascadeCount = static_cast<float>(clipmapCascadeCount);
                    pushConstants.clipmapCenterX = lastClipmapUpdatePos.x;
                    pushConstants.clipmapCenterZ = lastClipmapUpdatePos.z;

                    cmd_buf.pushConstants<PushConstants>(
                        terrainPipeline.getVkPipelineLayout(),
                        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eTessellationControl |
                        vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
                        0,
                        {pushConstants}
                    );

                    etna::flush_barriers(cmd_buf);
                    cmd_buf.draw(4, 1, 0, 0);
                }
            }
        }
    }

public:
    etna::Image mainViewDepth;
    etna::Image heightMap;
    etna::Image splatMap;
    etna::Image detailTextures;
    etna::Image detailNormals;
    etna::Sampler heightMapSampler;
    etna::Sampler splatMapSampler;
    etna::Sampler detailSampler;

    std::vector<etna::Image> clipmapCascades;
    etna::Sampler clipmapSampler;
    etna::GraphicsPipeline clipmapSplatPipeline{};
    uint32_t clipmapCascadeCount = 4;
    uint32_t clipmapResolution = 1024;
    float clipmapBaseSize = 100.0f;
    float clipmapUpdateThreshold = 10.0f;
    glm::vec3 lastClipmapUpdatePos{1e10f};
    bool forceClipmapUpdate = true;
    bool useClipmap = true;

    struct PushConstants {
        glm::mat4x4 viewProj;
        glm::vec3 cameraPos;
        float heightScale;
        glm::vec2 chunkOffset;
        float chunkSize;
        float tessellationFactor;
        float terrainSize;
        float detailTiling;
        float useClipmap;
        float clipmapBaseSize;
        float clipmapCascadeCount;
        float clipmapCenterX;
        float clipmapCenterZ;
        float padding;
    } pushConstants;

    glm::mat4x4 viewProj;
    glm::vec3 cameraPos;

    etna::GraphicsPipeline terrainPipeline{};

    glm::uvec2 resolution;

    float heightScale = 50.0f;
    float tessellationFactor = 64.0f;
    int gridSize = 8;
    float chunkSize = 100.0f;
    float detailTiling = 32.0f;
    bool wireframeMode = false;

    std::unique_ptr<TerrainGenerator> terrainGen;
};

export class TerrainGuiComponent : public World::Entity::Component {
public:
    void PreRender() override {
        auto renderer = GetOwner().GetComponent<TerrainRendererComponent>();
        if (!renderer) return;

        ImGui::Begin("Terrain Settings");

        ImGui::Text("Terrain Generation");
        ImGui::Separator();

        static int octaves = 6;
        if (ImGui::SliderInt("Octaves", &octaves, 1, 12)) {}

        if (ImGui::Button("Regenerate Terrain")) {
            renderer->heightMap = renderer->terrainGen->GenerateHeightMap(4096, 4096, octaves);
            renderer->splatMap = renderer->terrainGen->GenerateSplatMap(4096, 4096, octaves);
            renderer->forceClipmapUpdate = true;
            spdlog::info("Terrain regenerated with {} octaves", octaves);
        }

        ImGui::NewLine();
        ImGui::Text("Terrain Rendering");
        ImGui::Separator();

        ImGui::SliderFloat("Height Scale", &renderer->heightScale, 0.0f, 200.0f);
        ImGui::SliderFloat("Tessellation Factor", &renderer->tessellationFactor, 1.0f, 128.0f);
        ImGui::SliderFloat("Chunk Size", &renderer->chunkSize, 10.0f, 500.0f);
        ImGui::SliderInt("Grid Size", &renderer->gridSize, 1, 20);
        ImGui::SliderFloat("Detail Tiling", &renderer->detailTiling, 1.0f, 128.0f);

        ImGui::NewLine();
        ImGui::Text("Clipmap Settings");
        ImGui::Separator();

        ImGui::Checkbox("Use Clipmap", &renderer->useClipmap);
        ImGui::SliderFloat("Clipmap Base Size", &renderer->clipmapBaseSize, 50.0f, 500.0f);
        ImGui::SliderFloat("Clipmap Update Threshold", &renderer->clipmapUpdateThreshold, 1.0f, 50.0f);

        if (ImGui::Button("Force Clipmap Update")) renderer->forceClipmapUpdate = true;

        ImGui::NewLine();
        ImGui::Text(
            "Application average %.3f ms/frame (%.1f FPS)",
            1000.0f / ImGui::GetIO().Framerate,
            ImGui::GetIO().Framerate
        );

        ImGui::NewLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Controls:");
        ImGui::Text("T - Toggle wireframe mode");
        ImGui::Text("Arrow Keys - Adjust tessellation/height");
        ImGui::Text("B - Recompile and reload shaders");

        ImGui::End();
    }
};
