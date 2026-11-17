add_executable(instancing
  main.cpp
  App.cpp
  RockModel.cpp
  RockScatter.cpp
)

target_link_libraries(instancing
  PRIVATE glfw etna glm::glm wsi gui tinygltf)

target_add_shaders(instancing
  shaders/heightmap.comp
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/rocks.vert
  shaders/rocks.frag
)
