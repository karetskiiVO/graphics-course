module;

#include <optional>
#include <concepts>
#include <memory>
#include <span>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <iostream>

#include <tracy/Tracy.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/ShaderProgram.hpp>
#include <etna/Profiling.hpp>
#include <imgui.h>

#include "wsi/OsWindowingManager.hpp"
#include "wsi/Keyboard.hpp"
#include "scene/Camera.hpp"
#include "gui/ImGuiRenderer.hpp"
#include "function2/function2.hpp"
#include "FramePacket.hpp"

export module Graphics;

import TerrainGenerator;
import Engine;

export class IDispatcher {
public:
    virtual void /*TODO: Error*/ Dispatch() = 0;
    virtual ~IDispatcher() {}
};

export template<typename T>
concept CDispatcher = std::derived_from<T, IDispatcher> && std::default_initializable<T>;

export template <CDispatcher Dispatcher1, CDispatcher Dispatcher2>
class DispatchChain : public Dispatcher2, public IDispatcher {
public:
    explicit DispatchChain(Dispatcher1& dispatcher1) : dispatcher(dispatcher1) {}

    void Dispatch() override {
        dispatcher.Dispatch();
        dynamic_cast<Dispatcher2*>(this)->Dispatch();
    }
private:
    Dispatcher1& dispatcher;
};

export class ShaderDispatcher;

export class ShaderAsset {
public:
    ShaderAsset(std::string name) : resourceName(std::move(name)) {}

    ShaderAsset& SetPath(std::string path) {
        sourcePaths.push_back(std::move(path));
        return *this;
    }

    ShaderAsset& Load() {
        std::vector<std::filesystem::path> spvPaths;
        for (const auto& sourcePath : sourcePaths) {
            std::filesystem::path src(sourcePath);
            std::filesystem::path spv = sourcePath + ".spv";

            bool needsCompile = false;
            needsCompile |= !std::filesystem::exists(spv);
            needsCompile |= std::filesystem::exists(src) && std::filesystem::last_write_time(src) > std::filesystem::last_write_time(spv);
            needsCompile &= std::filesystem::exists(src);

            if (needsCompile) {
                auto cmd = std::format("glslangValidator -g -V \"{}\" -o \"{}\"", src.string(), spv.string());
                std::system(cmd.c_str());
            }
            spvPaths.push_back(spv);
        }

        if (etna::get_program_id(resourceName.c_str()) == etna::ShaderProgramId::Invalid) {
            etna::get_context().getShaderManager().loadProgram(resourceName.c_str(), spvPaths);
        }
        return *this;
    }

    ShaderDispatcher CreateDispatch();
private:
    std::string resourceName;
    std::vector<std::string> sourcePaths;
};

export class ShaderDispatcher : public IDispatcher {
public:
    ShaderDispatcher() = default;
    ShaderDispatcher(std::string name) : resourceName(std::move(name)) {}

    ShaderDispatcher& Name(std::string name) {
        std::swap(resourceName, name);
        return *this;
    }

    ShaderDispatcher& Size(glm::uvec2 sz) {
        extent = vk::Extent3D{sz.x, sz.y, 1};
        return *this;
    }

    ShaderDispatcher& Size(uint32_t w, uint32_t h) {
        extent = vk::Extent3D{w, h, 1};
        return *this;
    }

    ShaderDispatcher& Format(vk::Format fmt) {
        imageFormat = fmt;
        return *this;
    }

    ShaderDispatcher& UseColorAttachment() {
        usage |= vk::ImageUsageFlagBits::eColorAttachment;
        return *this;
    }

    ShaderDispatcher& UseSampled() {
        usage |= vk::ImageUsageFlagBits::eSampled;
        return *this;
    }

    ShaderDispatcher& BbindSampler(uint32_t /*set*/, uint32_t /*binding*/, vk::Sampler /*sampler*/) {
        return *this;
    }

    ShaderDispatcher& BindImage(uint32_t /*set*/, uint32_t /*binding*/, vk::ImageView /*view*/) {
        return *this;
    }

    ShaderDispatcher& BindBuffer(uint32_t /*set*/, uint32_t /*binding*/, vk::Buffer /*buffer*/) {
        return *this;
    }

    ShaderDispatcher& Init(vk::CommandBuffer* cmdBuf) {
        commandBuffer = cmdBuf;
        return *this;
    }

    void Dispatch() override {
    }

    template <CDispatcher Dispatcher>
    DispatchChain<ShaderDispatcher, Dispatcher> Chain() {
        return DispatchChain<ShaderDispatcher, Dispatcher>{*this};
    }

private:
    std::string resourceName;
    vk::Extent3D extent{};
    vk::Format imageFormat{};
    vk::ImageUsageFlags usage{};
    vk::CommandBuffer* commandBuffer = nullptr;
};

inline ShaderDispatcher ShaderAsset::CreateDispatch() {
    return ShaderDispatcher(resourceName);
}

export class EtnaRenderSystem : public World::System {
public:
    OsWindowingManager windowing;
    std::unique_ptr<OsWindow> mainWindow;
    std::unique_ptr<etna::Window> etnaWindow;
    std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
    std::unique_ptr<ImGuiRenderer> guiRenderer;

    glm::uvec2 resolution = {1280, 720};
    bool useVsync = true;

    vk::CommandBuffer currentCmdBuf{};
    vk::Image targetImage{};
    vk::ImageView targetImageView{};
    std::optional<etna::Window::SwapchainImage> nextSwapchainImage;

    vk::CommandBuffer GetCurrentCmdBuf() const { return currentCmdBuf; }
    vk::Image GetTargetImage() const { return targetImage; }
    vk::ImageView GetTargetImageView() const { return targetImageView; }

    void Awake() override {
        mainWindow = windowing.createWindow(OsWindow::CreateInfo{.resolution = resolution,});

        auto instExts = windowing.getRequiredVulkanInstanceExtensions();
        std::vector<const char*> instanceExtensions(instExts.begin(), instExts.end());
        std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        vk::PhysicalDeviceFeatures features{};
        features.tessellationShader = VK_TRUE;
        features.fillModeNonSolid = VK_TRUE;

        etna::initialize(etna::InitParams{
            .applicationName = "terrain",
            .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
            .instanceExtensions = instanceExtensions,
            .deviceExtensions = deviceExtensions,
            .features = vk::PhysicalDeviceFeatures2{.features = features},
            .physicalDeviceIndexOverride = {},
            .numFramesInFlight = 2,
        });
    }

    void Start() override {
        auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());
        auto& ctx = etna::get_context();
        commandManager = ctx.createPerFrameCmdMgr();
        etnaWindow = ctx.createWindow(etna::Window::CreateInfo{.surface = std::move(surface),});

        auto [w, h] = etnaWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
            .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
        });
        resolution = {w, h};

        guiRenderer = std::make_unique<ImGuiRenderer>(etnaWindow->getCurrentFormat());
        ImGuiRenderer::enableImGuiForWindow(mainWindow->native());
    }

    void PreRender() override {
        windowing.poll();
        if (mainWindow->isBeingClosed()) std::exit(0);

        auto cmdBuf = commandManager->acquireNext();
        etna::begin_frame();

        nextSwapchainImage = etnaWindow->acquireNext();

        if (nextSwapchainImage) {
            auto [image, view, availableSem, readyForPresentSem] = *nextSwapchainImage;

            this->currentCmdBuf = cmdBuf;
            this->targetImage = image;
            this->targetImageView = view;

            ETNA_CHECK_VK_RESULT(cmdBuf.begin(vk::CommandBufferBeginInfo{}));

            guiRenderer->nextFrame();
            ImGui::NewFrame();
        } else {
            this->currentCmdBuf = nullptr;
        }
    }

    void Dispose() override {
        ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
    }

    void Render() override;
};

export class TransformComponent : public World::Entity::Component {
public:
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

export class CameraComponent : public World::Entity::Component {
public:
    Camera mainCam;

    void Start() override {
        mainCam.lookAt({0, 50, 50}, {0, 0, 0}, {0, 1, 0});
        if (auto transform = GetOwner().GetComponent<TransformComponent>()) {
            transform->position = mainCam.position;
        }
    }
};

export class CameraControllerComponent : public World::Entity::Component {
public:
    float camMoveSpeed = 80.0f;
    float camRotateSpeed = 1.0f;

    void Update() override {
        auto cameraComp = GetOwner().GetComponent<CameraComponent>();
        if (!cameraComp) return;

        auto system = GetOwner().GetWorld()->GetSystem<EtnaRenderSystem>();
        auto timeSys = GetOwner().GetWorld()->GetSystem<TimeSystem>();
        auto kb = system->mainWindow->keyboard;
        auto ms = system->mainWindow->mouse;
        float dt = timeSys->DeltaTime();

        if (kb[KeyboardKey::kEscape] == ButtonState::Falling) system->mainWindow->askToClose();

        if (is_held_down(kb[KeyboardKey::kLeftShift])) camMoveSpeed = 200.0f;
        else camMoveSpeed = 80.0f;

        if (ms[MouseButton::mbRight] == ButtonState::Rising) system->mainWindow->captureMouse = !system->mainWindow->captureMouse;

        glm::vec3 dir = {0, 0, 0};
        if (is_held_down(kb[KeyboardKey::kS])) dir -= cameraComp->mainCam.forward();
        if (is_held_down(kb[KeyboardKey::kW])) dir += cameraComp->mainCam.forward();
        if (is_held_down(kb[KeyboardKey::kA])) dir -= cameraComp->mainCam.right();
        if (is_held_down(kb[KeyboardKey::kD])) dir += cameraComp->mainCam.right();
        if (is_held_down(kb[KeyboardKey::kF])) dir -= cameraComp->mainCam.up();
        if (is_held_down(kb[KeyboardKey::kR])) dir += cameraComp->mainCam.up();
        if (glm::length(dir) > 0.0001f) cameraComp->mainCam.position += glm::normalize(dir) * camMoveSpeed * dt;

        if (system->mainWindow->captureMouse) {
            cameraComp->mainCam.rotate(
                20 * ms.capturedPosDelta.y * camRotateSpeed * dt,
                20 * ms.capturedPosDelta.x * camRotateSpeed * dt
            );
        }

        if (auto transform = GetOwner().GetComponent<TransformComponent>()) {
            transform->position = cameraComp->mainCam.position;
        }
    }
};

void EtnaRenderSystem::Render() {
    ZoneScoped;

    if (!nextSwapchainImage) {
        if (windowing.getTime() >= 0) { // arbitrary validation to prevent missing width bounds
            auto [w, h] = etnaWindow->recreateSwapchain(etna::Window::DesiredProperties{
                .resolution = {resolution.x, resolution.y},
                .vsync = useVsync,
                .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
            });
            resolution = {w, h};
        }
        etna::end_frame();
        return;
    }

    auto cmdBuf = currentCmdBuf;
    auto [image, view, availableSem, readyForPresentSem] = std::move(*nextSwapchainImage);

    {
        ZoneScopedN("drawGui");
        ImGui::Render();
    }

    {
        ETNA_PROFILE_GPU(cmdBuf, renderFrame);

        {
            ImDrawData* pDrawData = ImGui::GetDrawData();
            guiRenderer->render(cmdBuf, {{0, 0}, {resolution.x, resolution.y}}, image, view, pDrawData);
        }

        etna::set_state(
            cmdBuf,
            image,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlags2{},
            vk::ImageLayout::ePresentSrcKHR,
            vk::ImageAspectFlagBits::eColor
        );

        etna::flush_barriers(cmdBuf);
        ETNA_READ_BACK_GPU_PROFILING(cmdBuf);
    }
    ETNA_CHECK_VK_RESULT(cmdBuf.end());

    auto renderingDone = commandManager->submit(
        std::move(cmdBuf),
        std::move(availableSem),
        std::move(readyForPresentSem)
    );

    const bool presented = etnaWindow->present(std::move(renderingDone), view);
    if (!presented) nextSwapchainImage = std::nullopt;

    if (!nextSwapchainImage && windowing.getTime() >= 0) { // arbitrary validation to prevent missing width bounds
        auto [w, h] = etnaWindow->recreateSwapchain(etna::Window::DesiredProperties{
            .resolution = {resolution.x, resolution.y},
            .vsync = useVsync,
            .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
        });
        resolution = {w, h};
    }

    etna::end_frame();
}
