module;

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#include <random>
#include <vector>
#include <cstring>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/ShaderProgram.hpp>
#include <etna/BlockingTransferHelper.hpp>

#include <function2/function2.hpp>
#include <imgui.h>

export module SsaoRenderer;
import Engine;
import Graphics;

export class SsaoRendererComponent : public World::Entity::Component {
public:
    bool enabled = true;
    float radius = 0.5f;
    float bias = 0.025f;
    int kernelSize = 32;
    float power = 2.0f;
    float strength = 1.0f;
    int blurSize = 2;
    float depthThreshold = 0.001f;

    void Start() override {
        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        resolution = system->resolution;

        generateKernel();
        generateNoiseTexture();
        createSsaoRenderTargets();
        createPipelines(system);
    }

    void PreRender() override {
        if (!enabled) return;

        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        auto cmdBuf = system->GetCurrentCmdBuf();
        if (!cmdBuf) return;

        auto cameraEntity = GetOwner().GetWorld()->GetEntity("MainCamera");
        if (!cameraEntity) return;
        auto cameraComp = cameraEntity->GetComponent<CameraComponent>();
        if (!cameraComp) return;

        float aspect = float(resolution.x) / float(resolution.y);
        glm::mat4 projection = cameraComp->mainCam.projTm(aspect);
        glm::mat4 invProjection = glm::inverse(projection);

        ETNA_PROFILE_GPU(cmdBuf, ssaoPass);

        {
            ETNA_PROFILE_GPU(cmdBuf, ssaoCompute);

            auto shaderInfo = etna::get_shader_program("ssao");
            auto descriptorSet = etna::create_descriptor_set(
                shaderInfo.getDescriptorLayoutId(0),
                cmdBuf,
                {
                    etna::Binding{0, system->sceneDepthImage.genBinding(nearestSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    etna::Binding{1, system->sceneNormalsImage.genBinding(linearSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    etna::Binding{2, noiseTexture.genBinding(noiseSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    etna::Binding{3, kernelBuffer.genBinding()},
                }
            );
            etna::flush_barriers(cmdBuf);

            etna::RenderTargetState renderTargets(
                cmdBuf,
                {{0, 0}, {resolution.x, resolution.y}},
                {{.image = ssaoImage.get(), .view = ssaoImage.getView({})}},
                {}
            );

            cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, ssaoPipeline.getVkPipeline());

            cmdBuf.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                ssaoPipeline.getVkPipelineLayout(),
                0,
                {descriptorSet.getVkSet()},
                {}
            );

            struct SsaoPushConstants {
                glm::mat4 projection;
                glm::mat4 invProjection;
                glm::vec2 noiseScale;
                float radius;
                float bias;
                int kernelSize;
                float power;
                float padding1;
                float padding2;
            } pc;

            pc.projection = projection;
            pc.invProjection = invProjection;
            pc.noiseScale = glm::vec2(
                float(resolution.x) / float(NOISE_SIZE),
                float(resolution.y) / float(NOISE_SIZE)
            );
            pc.radius = radius;
            pc.bias = bias;
            pc.kernelSize = kernelSize;
            pc.power = power;
            pc.padding1 = 0.0f;
            pc.padding2 = 0.0f;

            cmdBuf.pushConstants<SsaoPushConstants>(
                ssaoPipeline.getVkPipelineLayout(),
                vk::ShaderStageFlagBits::eFragment,
                0,
                {pc}
            );

            cmdBuf.draw(3, 1, 0, 0);
        }

        {
            ETNA_PROFILE_GPU(cmdBuf, ssaoBlur);

            auto shaderInfo = etna::get_shader_program("ssao_blur");
            auto descriptorSet = etna::create_descriptor_set(
                shaderInfo.getDescriptorLayoutId(0),
                cmdBuf,
                {
                    etna::Binding{0, ssaoImage.genBinding(linearSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    etna::Binding{1, system->sceneDepthImage.genBinding(nearestSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                }
            );
            etna::flush_barriers(cmdBuf);

            etna::RenderTargetState renderTargets(
                cmdBuf,
                {{0, 0}, {resolution.x, resolution.y}},
                {{.image = ssaoBlurredImage.get(), .view = ssaoBlurredImage.getView({})}},
                {}
            );

            cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, ssaoBlurPipeline.getVkPipeline());

            cmdBuf.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                ssaoBlurPipeline.getVkPipelineLayout(),
                0,
                {descriptorSet.getVkSet()},
                {}
            );

            struct BlurPushConstants {
                glm::vec2 texelSize;
                int blurSize;
                float depthThreshold;
            } blurPC;

            blurPC.texelSize = glm::vec2(1.0f / float(resolution.x), 1.0f / float(resolution.y));
            blurPC.blurSize = blurSize;
            blurPC.depthThreshold = depthThreshold;

            cmdBuf.pushConstants<BlurPushConstants>(
                ssaoBlurPipeline.getVkPipelineLayout(),
                vk::ShaderStageFlagBits::eFragment,
                0,
                {blurPC}
            );

            cmdBuf.draw(3, 1, 0, 0);
        }

        {
            ETNA_PROFILE_GPU(cmdBuf, ssaoApply);

            auto shaderInfo = etna::get_shader_program("ssao_apply");
            auto descriptorSet = etna::create_descriptor_set(
                shaderInfo.getDescriptorLayoutId(0),
                cmdBuf,
                {
                    etna::Binding{0, system->sceneColorImage.genBinding(linearSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                    etna::Binding{1, ssaoBlurredImage.genBinding(linearSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                }
            );
            etna::flush_barriers(cmdBuf);

            etna::RenderTargetState renderTargets(
                cmdBuf,
                {{0, 0}, {resolution.x, resolution.y}},
                {{.image = ssaoApplyImage.get(), .view = ssaoApplyImage.getView({})}},
                {}
            );

            cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, ssaoApplyPipeline.getVkPipeline());

            cmdBuf.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                ssaoApplyPipeline.getVkPipelineLayout(),
                0,
                {descriptorSet.getVkSet()},
                {}
            );

            struct ApplyPushConstants {
                float ssaoStrength;
                float padding1;
                float padding2;
                float padding3;
            } applyPC;

            applyPC.ssaoStrength = strength;
            applyPC.padding1 = 0.0f;
            applyPC.padding2 = 0.0f;
            applyPC.padding3 = 0.0f;

            cmdBuf.pushConstants<ApplyPushConstants>(
                ssaoApplyPipeline.getVkPipelineLayout(),
                vk::ShaderStageFlagBits::eFragment,
                0,
                {applyPC}
            );

            cmdBuf.draw(3, 1, 0, 0);
        }

        {
            etna::set_state(
                cmdBuf,
                ssaoApplyImage.get(),
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferRead,
                vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageAspectFlagBits::eColor
            );

            etna::set_state(
                cmdBuf,
                system->sceneColorImage.get(),
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eTransferDstOptimal,
                vk::ImageAspectFlagBits::eColor
            );

            etna::flush_barriers(cmdBuf);

            vk::ImageCopy copyRegion{};
            copyRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            copyRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            copyRegion.extent = {resolution.x, resolution.y, 1};

            cmdBuf.copyImage(
                ssaoApplyImage.get(), vk::ImageLayout::eTransferSrcOptimal,
                system->sceneColorImage.get(), vk::ImageLayout::eTransferDstOptimal,
                {copyRegion}
            );
        }
    }

private:
    static constexpr uint32_t NOISE_SIZE = 4;
    static constexpr uint32_t MAX_KERNEL_SIZE = 64;

    glm::uvec2 resolution;

    etna::Image ssaoImage;
    etna::Image ssaoBlurredImage;
    etna::Image ssaoApplyImage;

    etna::Image noiseTexture;

    etna::Buffer kernelBuffer;

    etna::Sampler linearSampler;
    etna::Sampler nearestSampler;
    etna::Sampler noiseSampler;

    etna::GraphicsPipeline ssaoPipeline{};
    etna::GraphicsPipeline ssaoBlurPipeline{};
    etna::GraphicsPipeline ssaoApplyPipeline{};

    void generateKernel() {
        std::default_random_engine rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        std::vector<glm::vec4> samples(MAX_KERNEL_SIZE);

        for (uint32_t i = 0; i < MAX_KERNEL_SIZE; ++i) {
            glm::vec3 sample(
                dist(rng) * 2.0f - 1.0f,
                dist(rng) * 2.0f - 1.0f,
                dist(rng)
            );
            sample = glm::normalize(sample);
            sample *= dist(rng);

            float scale = float(i) / float(MAX_KERNEL_SIZE);
            scale = glm::mix(0.1f, 1.0f, scale * scale);
            sample *= scale;

            samples[i] = glm::vec4(sample, 0.0f);
        }

        auto& ctx = etna::get_context();
        kernelBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
            .size = MAX_KERNEL_SIZE * sizeof(glm::vec4),
            .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
            .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
            .name = "ssao_kernel",
        });

        auto oneShotCmdMgr = ctx.createOneShotCmdMgr();
        auto transferHelper = etna::BlockingTransferHelper{
            etna::BlockingTransferHelper::CreateInfo{
                .stagingSize = MAX_KERNEL_SIZE * sizeof(glm::vec4),
            }
        };
        transferHelper.uploadBuffer(
            *oneShotCmdMgr,
            kernelBuffer,
            0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(samples.data()),
                MAX_KERNEL_SIZE * sizeof(glm::vec4))
        );
    }

    void generateNoiseTexture() {
        std::default_random_engine rng(123);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        struct RGBA8 { uint8_t r, g, b, a; };
        std::vector<RGBA8> noiseData(NOISE_SIZE * NOISE_SIZE);

        for (uint32_t i = 0; i < NOISE_SIZE * NOISE_SIZE; ++i) {
            glm::vec3 noise(
                dist(rng) * 2.0f - 1.0f,
                dist(rng) * 2.0f - 1.0f,
                0.0f
            );
            noiseData[i] = {
                static_cast<uint8_t>((noise.x * 0.5f + 0.5f) * 255.0f),
                static_cast<uint8_t>((noise.y * 0.5f + 0.5f) * 255.0f),
                static_cast<uint8_t>((noise.z * 0.5f + 0.5f) * 255.0f),
                255
            };
        }

        auto& ctx = etna::get_context();
        noiseTexture = ctx.createImage(etna::Image::CreateInfo{
            .extent = vk::Extent3D{NOISE_SIZE, NOISE_SIZE, 1},
            .name = "ssao_noise",
            .format = vk::Format::eR8G8B8A8Unorm,
            .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        });

        auto oneShotCmdMgr = ctx.createOneShotCmdMgr();
        auto transferHelper = etna::BlockingTransferHelper{
            etna::BlockingTransferHelper::CreateInfo{
                .stagingSize = NOISE_SIZE * NOISE_SIZE * sizeof(RGBA8),
            }
        };
        transferHelper.uploadImage(
            *oneShotCmdMgr,
            noiseTexture,
            0, 0,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(noiseData.data()),
                NOISE_SIZE * NOISE_SIZE * sizeof(RGBA8)
            )
        );
    }

    void createSsaoRenderTargets() {
        auto& ctx = etna::get_context();

        ssaoImage = ctx.createImage(etna::Image::CreateInfo{
            .extent = vk::Extent3D{resolution.x, resolution.y, 1},
            .name = "ssao_raw",
            .format = vk::Format::eR8Unorm,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        });

        ssaoBlurredImage = ctx.createImage(etna::Image::CreateInfo{
            .extent = vk::Extent3D{resolution.x, resolution.y, 1},
            .name = "ssao_blurred",
            .format = vk::Format::eR8Unorm,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
        });

        ssaoApplyImage = ctx.createImage(etna::Image::CreateInfo{
            .extent = vk::Extent3D{resolution.x, resolution.y, 1},
            .name = "ssao_applied",
            .format = vk::Format::eR8G8B8A8Unorm,
            .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
        });

        linearSampler = etna::Sampler(etna::Sampler::CreateInfo{
            .filter = vk::Filter::eLinear,
            .addressMode = vk::SamplerAddressMode::eClampToEdge,
            .name = "ssao_linear_sampler",
        });

        nearestSampler = etna::Sampler(etna::Sampler::CreateInfo{
            .filter = vk::Filter::eNearest,
            .addressMode = vk::SamplerAddressMode::eClampToEdge,
            .name = "ssao_nearest_sampler",
        });

        noiseSampler = etna::Sampler(etna::Sampler::CreateInfo{
            .filter = vk::Filter::eNearest,
            .addressMode = vk::SamplerAddressMode::eRepeat,
            .name = "ssao_noise_sampler",
        });
    }

    void createPipelines(EtnaRenderSystem* /* system */) {
        auto& ctx = etna::get_context();
        auto& pm = ctx.getPipelineManager();

        etna::create_program("ssao", {
            SUPERTASK_SHADERS_ROOT "/fxaa.vert.spv",
            SUPERTASK_SHADERS_ROOT "/ssao.frag.spv",
        });

        ssaoPipeline = pm.createGraphicsPipeline("ssao", etna::GraphicsPipeline::CreateInfo{
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
                .colorAttachmentFormats = {vk::Format::eR8Unorm},
            },
        });

        etna::create_program("ssao_blur", {
            SUPERTASK_SHADERS_ROOT "/fxaa.vert.spv",
            SUPERTASK_SHADERS_ROOT "/ssao_blur.frag.spv",
        });

        ssaoBlurPipeline = pm.createGraphicsPipeline("ssao_blur", etna::GraphicsPipeline::CreateInfo{
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
                .colorAttachmentFormats = {vk::Format::eR8Unorm},
            },
        });

        etna::create_program("ssao_apply", {
            SUPERTASK_SHADERS_ROOT "/fxaa.vert.spv",  // Reuse fullscreen triangle vert
            SUPERTASK_SHADERS_ROOT "/ssao_apply.frag.spv",
        });

        ssaoApplyPipeline = pm.createGraphicsPipeline("ssao_apply", etna::GraphicsPipeline::CreateInfo{
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
        });
    }
};

export class SsaoGuiComponent : public World::Entity::Component {
public:
    void PreRender() override {
        auto ssao = GetOwner().GetComponent<SsaoRendererComponent>();
        if (!ssao) return;

        ImGui::Begin("SSAO Settings");

        ImGui::Checkbox("Enable SSAO", &ssao->enabled);

        if (ssao->enabled) {
            ImGui::Separator();
            ImGui::Text("SSAO Parameters");

            ImGui::SliderFloat("Radius", &ssao->radius, 0.1f, 5.0f, "%.2f");
            ImGui::SliderFloat("Bias", &ssao->bias, 0.001f, 0.1f, "%.3f");
            ImGui::SliderInt("Kernel Size", &ssao->kernelSize, 4, 64);
            ImGui::SliderFloat("Power", &ssao->power, 0.5f, 5.0f, "%.1f");
            ImGui::SliderFloat("Strength", &ssao->strength, 0.0f, 2.0f, "%.2f");

            ImGui::Separator();
            ImGui::Text("Blur Parameters");

            ImGui::SliderInt("Blur Size", &ssao->blurSize, 1, 4);
            ImGui::SliderFloat("Depth Threshold", &ssao->depthThreshold, 0.0001f, 0.01f, "%.4f");
        }

        ImGui::End();
    }
};
