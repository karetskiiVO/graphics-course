add_executable(skybox
  main.cpp
  App.cpp
  Skybox.cpp
  ../deferred/Decals.cpp
  ../deferred/RockModel.cpp
  ../deferred/RockScatter.cpp
)

target_include_directories(skybox PRIVATE ../deferred)
target_compile_definitions(skybox PRIVATE DEFERRED_SHADERS_ROOT=SKYBOX_SHADERS_ROOT)
target_link_libraries(skybox
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_add_shaders(skybox
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
  shaders/sky_bake.comp
  shaders/sky_noise.comp
)
