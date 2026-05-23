#include <compare>
#include <string_view>
#include <format>
#include <function2/function2.hpp>
#include <tracy/Tracy.hpp>
using namespace std::string_view_literals;

import Engine;
import Graphics;
import TerrainRenderer;
import FxaaRenderer;
import SsaoRenderer;

int main() {
    auto world = World();

    world.AddSystem<TimeSystem>();
    world.AddSystem<EtnaRenderSystem>();

    auto& cameraEntity = world.AddEntity("MainCamera");
    cameraEntity.AddComponent<TransformComponent>();
    cameraEntity.AddComponent<CameraComponent>();
    cameraEntity.AddComponent<CameraControllerComponent>();

    auto& terrainEntity = world.AddEntity("Terrain");
    terrainEntity.AddComponent<TransformComponent>();
    terrainEntity.AddComponent<TerrainRendererComponent>();
    terrainEntity.AddComponent<TerrainGuiComponent>();

    auto& ssaoEntity = world.AddEntity("SSAO");
    ssaoEntity.AddComponent<SsaoRendererComponent>();
    ssaoEntity.AddComponent<SsaoGuiComponent>();

    auto& fxaaEntity = world.AddEntity("PostProcessing");
    fxaaEntity.AddComponent<FxaaRendererComponent>();
    fxaaEntity.AddComponent<FxaaGuiComponent>();

    world.Run();
}
