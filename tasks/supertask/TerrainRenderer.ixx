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
        heightMapSampler = etna::Sampler(
            etna::Sampler::CreateInfo{.addressMode = vk::SamplerAddressMode::eClampToEdge, .name = "height_map_sampler",}
        );

        auto& pipelineManager = ctx.getPipelineManager();

        etna::create_program("terrain", {
            SUPERTASK_SHADERS_ROOT "/terrain.frag.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.vert.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.tesc.spv",
            SUPERTASK_SHADERS_ROOT "/terrain.tese.spv"
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
    }

    void Update() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        auto kb = system->mainWindow->keyboard;

        if (kb[KeyboardKey::kT] == ButtonState::Falling) {
            wireframeMode = !wireframeMode;
            spdlog::info("Wireframe mode: {}", wireframeMode ? "ON" : "OFF");
            // Recreate pipeline if wireframe changed
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
        const float aspect = float(resolution.x) / float(resolution.y);
        viewProj = cameraComponent->mainCam.projTm(aspect) * cameraComponent->mainCam.viewTm();

        {
            ETNA_PROFILE_GPU(cmd_buf, renderTerrain);

            etna::RenderTargetState renderTargets(
                cmd_buf,
                {{0, 0}, {resolution.x, resolution.y}},
                {{.image = target_image, .view = target_image_view}},
                {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}
            );

            cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainPipeline.getVkPipeline());
            
            auto terrainShaderInfo = etna::get_shader_program("terrain");

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
    }

public:
    etna::Image mainViewDepth;
    etna::Image heightMap;
    etna::Sampler heightMapSampler;

    struct PushConstants {
        glm::mat4x4 viewProj;
        glm::vec3 cameraPos;
        float heightScale;
        glm::vec2 chunkOffset;
        float chunkSize;
        float tessellationFactor;
    } pushConstants;

    glm::mat4x4 viewProj;
    glm::vec3 cameraPos;

    etna::GraphicsPipeline terrainPipeline{};

    glm::uvec2 resolution;

    float heightScale = 50.0f;
    float tessellationFactor = 64.0f;
    int gridSize = 8;
    float chunkSize = 100.0f;
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
            spdlog::info("Terrain regenerated with {} octaves", octaves);
        }

        ImGui::NewLine();
        ImGui::Text("Terrain Rendering");
        ImGui::Separator();

        ImGui::SliderFloat("Height Scale", &renderer->heightScale, 0.0f, 200.0f);
        ImGui::SliderFloat("Tessellation Factor", &renderer->tessellationFactor, 1.0f, 128.0f);
        ImGui::SliderFloat("Chunk Size", &renderer->chunkSize, 10.0f, 500.0f);
        ImGui::SliderInt("Grid Size", &renderer->gridSize, 1, 20);

        ImGui::NewLine();
        ImGui::Text(
            "Application average %.3f ms/frame (%.1f FPS)",
            1000.0f / ImGui::GetIO().Framerate,
            ImGui::GetIO().Framerate);

        ImGui::NewLine();
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Controls:");
        ImGui::Text("T - Toggle wireframe mode");
        ImGui::Text("Arrow Keys - Adjust tessellation/height");
        ImGui::Text("B - Recompile and reload shaders");

        ImGui::End();
    }
};