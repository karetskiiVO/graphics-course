add_executable(deferred
  main.cpp
  App.cpp
  Decals.cpp
  RockModel.cpp
  RockScatter.cpp
)

target_link_libraries(deferred
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_add_shaders(deferred
  shaders/heightmap.comp
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/rocks.vert
  shaders/rocks.frag
  shaders/fullscreen.vert
  shaders/decals.frag
  shaders/lighting.frag
)
