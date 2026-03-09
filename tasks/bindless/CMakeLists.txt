
add_executable(bindless
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  BindlessSceneManager.cpp
  Shader.cpp
)

target_link_libraries(bindless
  PRIVATE glfw etna glm::glm wsi scene render_utils)

target_add_shaders(bindless
  shaders/static_mesh.frag
  shaders/static_mesh.vert
)
