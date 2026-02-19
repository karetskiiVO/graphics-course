add_executable(imgui
  main.cpp
  Renderer.cpp
  WorldRenderer.cpp
  Shader.cpp
  RenderObject.cpp
  TerrainGenerator.cpp
  App.cpp
)

target_link_libraries(imgui
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(imgui
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
)
