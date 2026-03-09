#include "App.hpp"

#include <tracy/Tracy.hpp>

App::App () {
    glm::uvec2 initialRes = {1280, 720};
    mainWindow = windowing.createWindow(OsWindow::CreateInfo{.resolution = initialRes});

    renderer.reset(new Renderer(initialRes));

    auto instExts = windowing.getRequiredVulkanInstanceExtensions();
    renderer->InitVulkan(instExts);

    auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());
    renderer->InitFrameDelivery(
        std::move(surface), [this]() { return mainWindow->getResolution(); }
    );

    mainCam.lookAt({0.0f, 4.0f, 12.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
}

void App::Run () {
    double lastTime = windowing.getTime();
    while (!mainWindow->isBeingClosed()) {
        const double currTime  = windowing.getTime();
        const float  diffTime  = static_cast<float>(currTime - lastTime);
        lastTime = currTime;

        windowing.poll();
        ProcessInput(diffTime);
        DrawFrame();

        FrameMark;
    }
}

void App::ProcessInput (float dt) {
    ZoneScoped;

    if (mainWindow->keyboard[KeyboardKey::kEscape] == ButtonState::Falling) mainWindow->askToClose();
    camMoveSpeed = is_held_down(mainWindow->keyboard[KeyboardKey::kLeftShift]) ? 120.0f : 30.0f;

    if (mainWindow->mouse[MouseButton::mbRight] == ButtonState::Rising) mainWindow->captureMouse = !mainWindow->captureMouse;

    MoveCam(mainCam, mainWindow->keyboard, dt);
    if (mainWindow->captureMouse) RotateCam(mainCam, mainWindow->mouse);

    renderer->DebugInput(mainWindow->keyboard);
}

void App::DrawFrame () {
    ZoneScoped;

    renderer->Update(FramePacket{
        .mainCam     = mainCam,
        .currentTime = static_cast<float>(windowing.getTime()),
    });
    renderer->DrawFrame();
}

void App::MoveCam (Camera& cam, const Keyboard& kb, float dt) {
    const glm::vec3 fwd   = glm::normalize(glm::vec3(cam.forward().x, 0.0f, cam.forward().z));
    const glm::vec3 right = glm::normalize(glm::vec3(cam.right().x,   0.0f, cam.right().z));

    glm::vec3 dir = {0.0f, 0.0f, 0.0f};
    if (is_held_down(kb[KeyboardKey::kW])) dir += fwd;
    if (is_held_down(kb[KeyboardKey::kS])) dir -= fwd;
    if (is_held_down(kb[KeyboardKey::kD])) dir += right;
    if (is_held_down(kb[KeyboardKey::kA])) dir -= right;
    if (is_held_down(kb[KeyboardKey::kR])) dir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (is_held_down(kb[KeyboardKey::kF])) dir -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length(dir) > 0.0001f) cam.position += glm::normalize(dir) * camMoveSpeed * dt;
}

void App::RotateCam (Camera& cam, const Mouse& ms) {
    cam.rotate(
        ms.capturedPosDelta.y * camRotateSpeed,
        ms.capturedPosDelta.x * camRotateSpeed
    );
}
