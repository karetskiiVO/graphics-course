add_executable(inflight_frames
  main.cpp
  App.cpp
)

target_link_libraries(inflight_frames
  PRIVATE glfw etna glm::glm wsi gui scene Tracy::TracyClient)

target_add_shaders(inflight_frames
  shaders/toy.frag
  shaders/toy.vert
  shaders/texture.frag
)