#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>


App::App()
    : resolution{1280, 720}
    , useVsync{true}
{
    {
        auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();
        std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};
        std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        etna::initialize(etna::InitParams{
            .applicationName = "Local Shadertoy 2",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .instanceExtensions = instanceExtensions,
            .deviceExtensions = deviceExtensions,
            .physicalDeviceIndexOverride = {},
            .numFramesInFlight = 1,
        });
    }

    auto& context = etna::get_context();

    osWindow = windowing.createWindow(OsWindow::CreateInfo{
        .resolution = resolution,
    });

    {
        auto surface = osWindow->createVkSurface(context.getInstance());
        vkWindow = context.createWindow(etna::Window::CreateInfo{
            .surface = std::move(surface),
        });

        auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
            .autoGamma = false,
        });
        resolution = {w, h};
    }

    commandManager = context.createPerFrameCmdMgr();

    etna::create_program("local shadertoy 2 buffer A", {
        LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv",
        LOCAL_SHADERTOY2_SHADERS_ROOT "bufferA.frag.spv",
    });
    etna::create_program("local shadertoy 2 buffer B", {
        LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv",
        LOCAL_SHADERTOY2_SHADERS_ROOT "bufferB.frag.spv",
    });
    etna::create_program("local shadertoy 2", {
        LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv",
        LOCAL_SHADERTOY2_SHADERS_ROOT "toy.frag.spv",
    });

    const auto bufferFormat = vk::Format::eR16G16B16A16Sfloat;
    bufferA = context.createImage({
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "buffer A: albedo and height",
        .format = bufferFormat,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    });
    bufferB = context.createImage({
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "buffer B: roughness, metallic, rust and cavity",
        .format = bufferFormat,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
    });

    auto& pipelineManager = context.getPipelineManager();
    etna::GraphicsPipeline::CreateInfo pipelineInfo{
        .depthConfig = {
            .depthTestEnable = vk::False,
            .depthWriteEnable = vk::False,
        },
        .fragmentShaderOutput = {
            .colorAttachmentFormats = {bufferFormat},
        },
    };
    bufferAPipeline = pipelineManager.createGraphicsPipeline("local shadertoy 2 buffer A", pipelineInfo);
    bufferBPipeline = pipelineManager.createGraphicsPipeline("local shadertoy 2 buffer B", pipelineInfo);

    pipelineInfo.fragmentShaderOutput.colorAttachmentFormats = {vkWindow->getCurrentFormat()};
    pipeline = pipelineManager.createGraphicsPipeline("local shadertoy 2", pipelineInfo);

    sampler = etna::Sampler({
        .filter = vk::Filter::eLinear,
        .addressMode = vk::SamplerAddressMode::eRepeat,
        .name = "procedural texture sampler",
        .maxLod = 0.0f,
    });
}

void App::Update() {
    glm::vec2 mouse = osWindow->mouse.freePos;

    pushConstants = PushConstants{
        .resolutionX = resolution.x,
        .resolutionY = resolution.y,
        .mouseX = mouse.x,
        .mouseY = mouse.y,
        .time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count(),
    };
}

App::~App() {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::Run() {
    start = std::chrono::steady_clock::now();

    while (!osWindow->isBeingClosed()) {
        windowing.poll();

        Update();
        DrawFrame();
    }

    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::DrawProceduralTexture(
    vk::CommandBuffer cmd, const etna::Image& target, const etna::GraphicsPipeline& texturePipeline
) {
    etna::RenderTargetState renderTargets(
        cmd,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target.get(), .view = target.getView({})}},
        {}
    );

    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, texturePipeline.getVkPipeline());
    cmd.pushConstants(
        texturePipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eFragment,
        0,
        sizeof(pushConstants),
        &pushConstants
    );
    cmd.draw(3, 1, 0, 0);
}

void App::DrawFrame() {
    auto currentCmdBuf = commandManager->acquireNext();
    etna::begin_frame();
    auto nextSwapchainImage = vkWindow->acquireNext();

    if (nextSwapchainImage) {
        auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

        ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
        {
            DrawProceduralTexture(currentCmdBuf, bufferA, bufferAPipeline);
            DrawProceduralTexture(currentCmdBuf, bufferB, bufferBPipeline);

            auto programInfo = etna::get_shader_program("local shadertoy 2");

            const auto descriptorSet = etna::create_descriptor_set(
                programInfo.getDescriptorLayoutId(0),
                currentCmdBuf,
                {etna::Binding{0, bufferA.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                etna::Binding{1, bufferB.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}}
            );

            {
                etna::RenderTargetState renderTargets(
                    currentCmdBuf,
                    {{0, 0}, {resolution.x, resolution.y}},
                    {{.image = backbuffer, .view = backbufferView}},
                    {}
                );

                currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
                currentCmdBuf.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    pipeline.getVkPipelineLayout(),
                    0,
                    {descriptorSet.getVkSet()},
                    {}
                );
                currentCmdBuf.pushConstants(
                    pipeline.getVkPipelineLayout(),
                    vk::ShaderStageFlagBits::eFragment,
                    0,
                    sizeof(pushConstants),
                    &pushConstants
                );
                currentCmdBuf.draw(3, 1, 0, 0);
            }

            etna::set_state(
                currentCmdBuf,
                backbuffer,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                {},
                vk::ImageLayout::ePresentSrcKHR,
                vk::ImageAspectFlagBits::eColor
            );
            etna::flush_barriers(currentCmdBuf);
        }
        ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

        auto renderingDone = commandManager->submit(
            std::move(currentCmdBuf), std::move(backbufferAvailableSem)
        );
        const bool presented = Present(renderingDone, backbufferView);

        if (!presented) nextSwapchainImage = std::nullopt;
    }

    etna::end_frame();

    if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0}) {
        ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
        auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
            .autoGamma = false,
        });
        presentSemaphores.clear();
        ETNA_VERIFY((resolution == glm::uvec2{w, h}));
    }
}

bool App::Present(vk::Semaphore renderingDone, vk::ImageView backbufferView) {
    auto& context = etna::get_context();
    auto& presentReady = presentSemaphores[backbufferView];
    if (!presentReady) presentReady = etna::unwrap_vk_result(context.getDevice().createSemaphoreUnique({}));

    const vk::SemaphoreSubmitInfo wait{
        .semaphore = renderingDone,
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    const vk::SemaphoreSubmitInfo signal{
        .semaphore = presentReady.get(),
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &wait,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signal,
    };
    ETNA_CHECK_VK_RESULT(context.getQueue().submit2({submitInfo}, {}));

    return vkWindow->present(presentReady.get(), backbufferView);
}
