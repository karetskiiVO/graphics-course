#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <stb_image.h>
#include <etna/BlockingTransferHelper.hpp>

App::App()
    : resolution{1280, 720}
    , useVsync{true}
{
    // First, we need to initialize Vulkan, which is not trivial because
    // extensions are required for just about anything.
    {
        // GLFW tells us which extensions it needs to present frames to the OS window.
        // Actually rendering anything to a screen is optional in Vulkan, you can
        // alternatively save rendered frames into files, send them over network, etc.
        // Instance extensions do not depend on the actual GPU, only on the OS.
        auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

        std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

        // We also need the swapchain device extension to get access to the OS
        // window from inside of Vulkan on the GPU.
        // Device extensions require HW support from the GPU.
        // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
        std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        // Etna does all of the Vulkan initialization heavy lifting.
        // You can skip figuring out how it works for now.
        etna::initialize(etna::InitParams{
            .applicationName = "Local Shadertoy",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .instanceExtensions = instanceExtensions,
            .deviceExtensions = deviceExtensions,
            // Replace with an index if etna detects your preferred GPU incorrectly
            .physicalDeviceIndexOverride = {},
            .numFramesInFlight = 1,
        });
    }

    auto& context = etna::get_context();

    // Now we can create an OS window
    osWindow = windowing.createWindow(OsWindow::CreateInfo{
        .resolution = resolution,
    });

    // But we also need to hook the OS window up to Vulkan manually!
    {
        // First, we ask GLFW to provide a "surface" for the window,
        // which is an opaque description of the area where we can actually render.
        auto surface = osWindow->createVkSurface(context.getInstance());

        // Then we pass it to Etna to do the complicated work for us
        vkWindow = context.createWindow(etna::Window::CreateInfo{
            .surface = std::move(surface),
        });

        // And finally ask Etna to create the actual swapchain so that we can
        // get (different) images each frame to render stuff into.
        // Here, we do not support window resizing, so we only need to call this once.
        auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
        });

        // Technically, Vulkan might fail to initialize a swapchain with the requested
        // resolution and pick a different one. This, however, does not occur on platforms
        // we support. Still, it's better to follow the "intended" path.
        resolution = {w, h};
    }

    // Next, we need a magical Etna helper to send commands to the GPU.
    // How it is actually performed is not trivial, but we can skip this for now.
    commandManager = context.createPerFrameCmdMgr();


    etna::create_program(
        "texture", 
        {LOCAL_SHADERTOY2_SHADERS_ROOT "texture.frag.spv", LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv"}
    );

    texturePipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
        "texture",
        etna::GraphicsPipeline::CreateInfo{
            .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Unorm},}
        }
    );

    sampler = etna::Sampler{ etna::Sampler::CreateInfo{
        .addressMode = vk::SamplerAddressMode::eMirroredRepeat, 
        .name = "sampler"
    }};
  
    image = context.createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{resolution.x, resolution.y, 1},
        .name = "texture",
        .format = vk::Format::eB8G8R8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment 
    });  

    etna::create_program(
        "shader", 
        { LOCAL_SHADERTOY2_SHADERS_ROOT "toy.frag.spv", LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv"}
    );

    graphicsPipeline = context.getPipelineManager().createGraphicsPipeline(
        "shader",
        etna::GraphicsPipeline::CreateInfo{
            .fragmentShaderOutput = {.colorAttachmentFormats = {vk::Format::eB8G8R8A8Unorm}}
        }
    );

    int w, h, chans;
    stbi_uc* pixels = stbi_load(
        GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png",
        &w,
        &h,
        &chans,
        STBI_rgb_alpha
    );

    VkDeviceSize imageSize = w * h * 4;

    assert(!pixels && "failed to load texture image!");

    auto texture = etna::get_context().createImage(etna::Image::CreateInfo{
        .extent = vk::Extent3D{static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1},
        .name = "texture",
        .format = vk::Format::eR8G8B8A8Unorm,
        .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
    });

    std::unique_ptr<etna::OneShotCmdMgr> oneShotCmdMgr = etna::get_context().createOneShotCmdMgr();
    auto blockingTransferHelper = etna::BlockingTransferHelper{
        etna::BlockingTransferHelper::CreateInfo{.stagingSize = static_cast<std::uint64_t>(imageSize)}
    };
    blockingTransferHelper.uploadImage(
        *oneShotCmdMgr, 
        texture, 
        0, 
        0,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(pixels), imageSize)
    );

    stbi_image_free(pixels);
}

void App::update() {
    glm::vec2 mouse = osWindow.get()->mouse.freePos;

    pushConstants = PushConstants{
        .resolutionX = resolution.x,
        .resolutionY = resolution.y,
        .mouseX      = mouse.x,
        .mouseY      = mouse.y,
        .time        = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count(),
    };
}

App::~App()
{
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
    start = std::chrono::steady_clock::now();

    while (!osWindow->isBeingClosed())
    {
        windowing.poll();
        
        update();
        drawFrame();
    }

    // We need to wait for the GPU to execute the last frame before destroying
    // all resources and closing the application.
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
    // First, get a command buffer to write GPU commands into.
    auto currentCmdBuf = commandManager->acquireNext();

    // Next, tell Etna that we are going to start processing the next frame.
    etna::begin_frame();

    // And now get the image we should be rendering the picture into.
    auto nextSwapchainImage = vkWindow->acquireNext();

    // When window is minimized, we can't render anything in Windows
    // because it kills the swapchain, so we skip frames in this case.
    if (nextSwapchainImage)
    {
        auto [backbuffer, backbufferView, backbufferAvailableSem, backbufferReadyForPresentSem] = *nextSwapchainImage;

        ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
        {
            etna::set_state(
                currentCmdBuf,
                image.get(),
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor
            );
            etna::flush_barriers(currentCmdBuf);

            {
                auto state = etna::RenderTargetState{
                    currentCmdBuf,
                    {{}, {resolution.x, resolution.y}},
                    {{image.get(), image.getView({})}},
                    {},
                };
                currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, texturePipeline.getVkPipeline());
                
                currentCmdBuf.pushConstants(
                    texturePipeline.getVkPipelineLayout(), 
                    vk::ShaderStageFlagBits::eFragment, 
                    0, 
                    sizeof(pushConstants), 
                    &pushConstants
                );

                currentCmdBuf.draw(3, 1, 0, 0);
            }


            etna::set_state(
                currentCmdBuf,
                image.get(),
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderRead,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor
            );
            etna::flush_barriers(currentCmdBuf);

            {
                auto state = etna::RenderTargetState{
                    currentCmdBuf,
                    {{}, {resolution.x, resolution.y}},
                    {{backbuffer, backbufferView}},
                    {},
                };

                auto set = etna::create_descriptor_set(
                    etna::get_shader_program("shader").getDescriptorLayoutId(0),
                    currentCmdBuf,
                    {
                        etna::Binding{0, image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
                        etna::Binding{1, texture.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}
                    }
                );

                auto vkSet = set.getVkSet();
                currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline.getVkPipeline());
                currentCmdBuf.bindDescriptorSets( 
                    vk::PipelineBindPoint::eGraphics, 
                    graphicsPipeline.getVkPipelineLayout(), 
                    0, 
                    1, 
                    &vkSet, 
                    0, 
                    nullptr
                );

                currentCmdBuf.pushConstants(
                    graphicsPipeline.getVkPipelineLayout(), 
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

        // We are done recording GPU commands now and we can send them to be executed by the GPU.
        // Note that the GPU won't start executing our commands before the semaphore is
        // signalled, which will happen when the OS says that the next swapchain image is ready.
        auto renderingDone = commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem), std::move(backbufferReadyForPresentSem));

        // Finally, present the backbuffer the screen, but only after the GPU tells the OS
        // that it is done executing the command buffer via the renderingDone semaphore.
        const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

        if (!presented) nextSwapchainImage = std::nullopt;
    }

    etna::end_frame();

    // After a window us un-minimized, we need to restore the swapchain to continue rendering.
    if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
    {
        auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
        });
        ETNA_VERIFY((resolution == glm::uvec2{w, h}));
    }
}
