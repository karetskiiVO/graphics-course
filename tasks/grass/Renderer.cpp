#include "Renderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

Renderer::Renderer (glm::uvec2 res)
    : resolution{res}
{
}

Renderer::~Renderer () {}

void Renderer::InitVulkan (std::span<const char*> extensions)
{
    std::vector<const char*> instanceExtensions(extensions.begin(), extensions.end());
    std::vector<const char*> deviceExtensions;
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    etna::initialize(etna::InitParams{
        .applicationName    = "grass",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .instanceExtensions = instanceExtensions,
        .deviceExtensions   = deviceExtensions,
        .numFramesInFlight  = 2,
    });
}

void Renderer::InitFrameDelivery (vk::UniqueSurfaceKHR surface, ResolutionProvider provider)
{
    resolutionProvider = std::move(provider);

    auto& ctx = etna::get_context();

    commandManager = ctx.createPerFrameCmdMgr();
    window = ctx.createWindow(etna::Window::CreateInfo{.surface = std::move(surface)});

    auto [w, h] = window->recreateSwapchain(etna::Window::DesiredProperties{
        .resolution        = {resolution.x, resolution.y},
        .vsync             = useVsync,
        .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
    });
    resolution = {w, h};

    worldRenderer = std::make_unique<WorldRenderer>();
    worldRenderer->AllocateResources(resolution);
    worldRenderer->LoadShaders();
    worldRenderer->SetupPipelines(window->getCurrentFormat());
}

void Renderer::DebugInput (const Keyboard& kb)
{
    worldRenderer->DebugInput(kb);

    if (kb[KeyboardKey::kB] == ButtonState::Falling)
    {
        const int retval = std::system(
            "cd " GRAPHICS_COURSE_ROOT "/build"
            " && cmake --build . --target grass_shaders");
        if (retval != 0)
        {
            spdlog::warn("Shader recompilation returned a non-zero return code!");
        }
        else
        {
            ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
            etna::reload_shaders();
            spdlog::info("Successfully reloaded shaders!");
        }
    }
}

void Renderer::Update (const FramePacket& packet)
{
    worldRenderer->Update(packet);
}

void Renderer::DrawFrame ()
{
    ZoneScoped;

    auto currentCmdBuf = commandManager->acquireNext();

    etna::begin_frame();

    auto nextSwapchainImage = window->acquireNext();

    if (nextSwapchainImage)
    {
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
                vk::ImageAspectFlagBits::eColor);

            etna::flush_barriers(currentCmdBuf);

            ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
        }
        ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

        auto renderingDone = commandManager->submit(
            std::move(currentCmdBuf),
            std::move(availableSem),
            std::move(readyForPresentSem));

        const bool presented = window->present(std::move(renderingDone), view);

        if (!presented)
            nextSwapchainImage = std::nullopt;
    }

    etna::end_frame();
}
