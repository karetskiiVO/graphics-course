#include <compare>
#include <string_view>
#include <format>
#include <function2/function2.hpp>
#include <tracy/Tracy.hpp>
using namespace std::string_view_literals;

import Engine;
import Graphics;
import TerrainRenderer;

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

    world.Run();
}
