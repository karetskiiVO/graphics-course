module;

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/ShaderProgram.hpp>

#include <function2/function2.hpp>
#include <imgui.h>

export module FxaaRenderer;
import Engine;
import Graphics;

export enum class AAMode : int {
    None = 0,
    FxaaBasic = 1,
    Fxaa311 = 2,
};

export class FxaaRendererComponent : public World::Entity::Component, public IPostProcessEffect {
public:
    AAMode aaMode = AAMode::FxaaBasic;
    float subpixelQuality = 0.75f;
    float edgeThreshold = 0.166f;
    float edgeThresholdMin = 0.0833f;

    void Start() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();

        createFxaaPipeline(system);

        system->SetPostProcessEffect(this);
    }

    void Dispose() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        if (system && system->postProcessEffect == this) system->SetPostProcessEffect(nullptr);
    }

    bool Apply(
        vk::CommandBuffer cmdBuf,
        const etna::Image& sceneImage, vk::ImageView /* sceneImageView */,
        vk::Image targetImage, vk::ImageView targetImageView,
        glm::uvec2 resolution
    ) override {
        ETNA_PROFILE_GPU(cmdBuf, fxaaPass);

        auto fxaaShaderInfo = etna::get_shader_program("fxaa");
        auto descriptorSet = etna::create_descriptor_set(
            fxaaShaderInfo.getDescriptorLayoutId(0),
            cmdBuf,
            {
                etna::Binding{0, sceneImage.genBinding(sceneSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            }
        );
        etna::flush_barriers(cmdBuf);

        etna::RenderTargetState renderTargets(
            cmdBuf,
            {{0, 0}, {resolution.x, resolution.y}},
            {{.image = targetImage, .view = targetImageView}},
            {}
        );

        cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, fxaaPipeline.getVkPipeline());

        cmdBuf.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            fxaaPipeline.getVkPipelineLayout(),
            0,
            {descriptorSet.getVkSet()},
            {}
        );

        FxaaPushConstants pc{};
        pc.inverseScreenSize = glm::vec2(1.0f / float(resolution.x), 1.0f / float(resolution.y));
        pc.aaMode = static_cast<int>(aaMode);
        pc.subpixelQuality = subpixelQuality;
        pc.edgeThreshold = edgeThreshold;
        pc.edgeThresholdMin = edgeThresholdMin;

        cmdBuf.pushConstants<FxaaPushConstants>(
            fxaaPipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eFragment,
            0,
            {pc}
        );

        cmdBuf.draw(3, 1, 0, 0);

        return true;
    }

private:
    struct FxaaPushConstants {
        glm::vec2 inverseScreenSize;
        int aaMode;
        float subpixelQuality;
        float edgeThreshold;
        float edgeThresholdMin;
        float padding1;
        float padding2;
    };

    void createFxaaPipeline(EtnaRenderSystem* system) {
        auto& ctx = etna::get_context();

        etna::create_program("fxaa", {
            SUPERTASK_SHADERS_ROOT "/fxaa.vert.spv",
            SUPERTASK_SHADERS_ROOT "/fxaa.frag.spv",
        });

        sceneSampler = etna::Sampler(etna::Sampler::CreateInfo{
            .filter = vk::Filter::eLinear,
            .addressMode = vk::SamplerAddressMode::eClampToEdge,
            .name = "fxaa_scene_sampler",
        });

        fxaaPipeline = ctx.getPipelineManager().createGraphicsPipeline(
            "fxaa",
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
                    .colorAttachmentFormats = {system->etnaWindow->getCurrentFormat()},
                },
            }
        );
    }

    etna::Sampler sceneSampler;
    etna::GraphicsPipeline fxaaPipeline{};
};

export class FxaaGuiComponent : public World::Entity::Component {
public:
    void PreRender() override {
        auto fxaa = GetOwner().GetComponent<FxaaRendererComponent>();
        if (!fxaa) return;

        ImGui::Begin("Anti-Aliasing Settings");

        const char* aaModes[] = { "None", "FXAA (Basic)", "FXAA 3.11 (Quality)" };
        int currentMode = static_cast<int>(fxaa->aaMode);
        if (ImGui::Combo("AA Mode", &currentMode, aaModes, 3)) {
            fxaa->aaMode = static_cast<AAMode>(currentMode);
        }

        if (fxaa->aaMode != AAMode::None) {
            ImGui::Separator();
            ImGui::Text("FXAA Parameters");

            ImGui::SliderFloat("Subpixel Quality", &fxaa->subpixelQuality, 0.0f, 1.0f, "%.2f (0=sharp, 1=soft)");
            ImGui::SliderFloat("Edge Threshold", &fxaa->edgeThreshold, 0.063f, 0.333f, "%.3f");
            ImGui::SliderFloat("Edge Threshold Min", &fxaa->edgeThresholdMin, 0.0312f, 0.0833f, "%.4f");

            ImGui::NewLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Basic FXAA: Simple edge detection, 8-step search");
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "FXAA 3.11: Full quality, 12-step variable search");
        }

        ImGui::End();
    }
};
