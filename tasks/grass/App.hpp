#pragma once

#include <etna/Etna.hpp>

#include "wsi/OsWindowingManager.hpp"
#include "scene/Camera.hpp"
#include "Renderer.hpp"

class App {
public:
    App ();

    App (const App&) = delete;
    App& operator= (const App&) = delete;

    ~App () {
        if (etna::is_initilized()) etna::shutdown();
    }

    void Run ();

private:
    void ProcessInput (float dt);
    void DrawFrame ();
    void MoveCam (Camera& cam, const Keyboard& kb, float dt);
    void RotateCam (Camera& cam, const Mouse& ms);

private:
    OsWindowingManager windowing;
    std::unique_ptr<OsWindow> mainWindow;

    float camMoveSpeed    = 30.0f;
    float camRotateSpeed  = 0.15f;
    Camera mainCam;

    std::unique_ptr<Renderer> renderer;
};
