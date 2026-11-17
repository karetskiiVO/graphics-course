add_executable(terrain
  main.cpp
  App.cpp
)

target_link_libraries(terrain
  PRIVATE glfw etna glm::glm wsi)

target_add_shaders(terrain
  shaders/heightmap.comp
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
)
