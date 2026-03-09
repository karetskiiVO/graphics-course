
add_executable(grass
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GrassRenderer.cpp
  Shader.cpp
)

target_link_libraries(grass
  PRIVATE glfw etna glm::glm wsi scene render_utils)

target_add_shaders(grass
  shaders/grass_placement.comp
  shaders/grass.vert
  shaders/grass.frag
)
