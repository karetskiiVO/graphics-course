#include "App.hpp"

#include <tracy/Tracy.hpp>

App::App () {
    glm::uvec2 initialRes = {1280, 720};
    mainWindow = windowing.createWindow(OsWindow::CreateInfo{.resolution = initialRes,});

    renderer.reset(new Renderer(initialRes));

    auto instExts = windowing.getRequiredVulkanInstanceExtensions();
    renderer->InitVulkan(instExts);

    auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());

    renderer->InitFrameDelivery(std::move(surface), [this] () { return mainWindow->getResolution(); });

    mainCam.lookAt({0, 10, 10}, {0, 0, 0}, {0, 1, 0});

    renderer->LoadScene(GRAPHICS_COURSE_RESOURCES_ROOT "/scenes/lovely_town/scene.gltf");
}

void App::Run () {
    double lastTime = windowing.getTime();
    while (!mainWindow->isBeingClosed()) {
        const double currTime = windowing.getTime();
        const float diffTime = static_cast<float>(currTime - lastTime);
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

    if (is_held_down(mainWindow->keyboard[KeyboardKey::kLeftShift])) camMoveSpeed = 10.0f;
    else camMoveSpeed = 1.0f;

    if (mainWindow->mouse[MouseButton::mbRight] ==
        ButtonState::Rising) mainWindow->captureMouse = !mainWindow->captureMouse;

    MoveCam(mainCam, mainWindow->keyboard, dt);
    if (mainWindow->captureMouse) RotateCam(mainCam, mainWindow->mouse, dt);

    renderer->DebugInput(mainWindow->keyboard);
}

void App::DrawFrame () {
    ZoneScoped;

    renderer->Update(FramePacket{.mainCam = mainCam, .currentTime = static_cast<float>(windowing.getTime()),});
    renderer->DrawFrame();
}

void App::MoveCam (Camera& cam, const Keyboard& kb, float dt) {
    glm::vec3 dir = {0, 0, 0};

    if (is_held_down(kb[KeyboardKey::kS])) dir -= cam.forward();

    if (is_held_down(kb[KeyboardKey::kW])) dir += cam.forward();

    if (is_held_down(kb[KeyboardKey::kA])) dir -= cam.right();

    if (is_held_down(kb[KeyboardKey::kD])) dir += cam.right();

    if (is_held_down(kb[KeyboardKey::kF])) dir -= cam.up();

    if (is_held_down(kb[KeyboardKey::kR])) dir += cam.up();

    if (glm::length(dir) > 0.0001f) cam.position += glm::normalize(dir) * camMoveSpeed * dt;
}

void App::RotateCam (Camera& cam, const Mouse& ms, float dt) {
    cam.rotate(
        20 * ms.capturedPosDelta.y * camRotateSpeed * dt,
        20 * ms.capturedPosDelta.x * camRotateSpeed * dt
    );
}

