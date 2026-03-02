#include "Renderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

Renderer::Renderer (glm::uvec2 res)
    : resolution{res} {}

void Renderer::InitVulkan (std::span<const char*> extensions) {
    std::vector<const char*> instanceExtensions;
    for (auto ext : extensions) instanceExtensions.push_back(ext);

    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    vk::PhysicalDeviceFeatures features{};
    features.multiDrawIndirect = VK_TRUE;

    auto descriptorIndexingFeatures =
        vk::PhysicalDeviceDescriptorIndexingFeatures{}
            .setShaderSampledImageArrayNonUniformIndexing(VK_TRUE)
            .setDescriptorBindingPartiallyBound(VK_TRUE)
            .setDescriptorBindingVariableDescriptorCount(VK_TRUE)
            .setRuntimeDescriptorArray(VK_TRUE);

    etna::initialize(etna::InitParams{
        .applicationName = "bindless",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .instanceExtensions = instanceExtensions,
        .deviceExtensions = deviceExtensions,
        .features = vk::PhysicalDeviceFeatures2{
            .pNext = &descriptorIndexingFeatures,
            .features = features,
        },
        .physicalDeviceIndexOverride = {},
        .numFramesInFlight = 2,
    });
}

void Renderer::InitFrameDelivery (vk::UniqueSurfaceKHR surface, ResolutionProvider provider) {
    resolutionProvider = std::move(provider);

    auto& ctx = etna::get_context();

    commandManager = ctx.createPerFrameCmdMgr();

    window = ctx.createWindow(etna::Window::CreateInfo{.surface = std::move(surface),});

    auto [w, h] = window->recreateSwapchain(etna::Window::DesiredProperties{
        .resolution = {resolution.x, resolution.y},
        .vsync = useVsync,
        .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
    });

    resolution = {w, h};

    worldRenderer = std::make_unique<WorldRenderer>();

    worldRenderer->AllocateResources(resolution);
    worldRenderer->LoadShaders();
    worldRenderer->SetupPipelines(window->getCurrentFormat());
}

void Renderer::LoadScene (std::filesystem::path path) {
    worldRenderer->LoadScene(std::move(path));
}

void Renderer::DebugInput (const Keyboard& kb) {
    worldRenderer->DebugInput(kb);

    if (kb[KeyboardKey::kB] == ButtonState::Falling) {
        const int retval = std::system("cd " GRAPHICS_COURSE_ROOT "/build"
                                       " && cmake --build . --target bindless_shaders");
        if (retval != 0) {
            spdlog::warn("Shader recompilation returned a non-zero return code!");
        } else {
            ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
            etna::reload_shaders();
            spdlog::info("Successfully reloaded shaders!");
        }
    }
}

void Renderer::Update (const FramePacket& packet) { worldRenderer->Update(packet); }

void Renderer::DrawFrame () {
    ZoneScoped;

    auto currentCmdBuf = commandManager->acquireNext();

    etna::begin_frame();

    auto nextSwapchainImage = window->acquireNext();

    if (nextSwapchainImage) {
        auto [image, view, availableSem, readyForPresentSem] = *nextSwapchainImage;

        ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
        {
            ETNA_PROFILE_GPU(currentCmdBuf, renderFrame);

            worldRenderer->RenderWorld(currentCmdBuf, image, view);

            etna::set_state(
                currentCmdBuf,
                image,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                {},
                vk::ImageLayout::ePresentSrcKHR,
                vk::ImageAspectFlagBits::eColor
            );

            etna::flush_barriers(currentCmdBuf);

            ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
        }
        ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

        auto renderingDone = commandManager->submit(
            std::move(currentCmdBuf),
            std::move(availableSem),
            std::move(readyForPresentSem)
        );

        const bool presented = window->present(std::move(renderingDone), view);

        if (!presented) nextSwapchainImage = std::nullopt;
    }

    if (!nextSwapchainImage && resolutionProvider() != glm::uvec2{0, 0}) {
        auto [w, h] = window->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
            .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
        });
        ETNA_VERIFY((resolution == glm::uvec2{w, h}));
    }

    etna::end_frame();
}

Renderer::~Renderer () { ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle()); }

