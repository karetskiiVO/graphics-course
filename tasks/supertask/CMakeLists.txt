add_executable(supertask
        main.cpp
        Graphics.ixx
        TerrainGenerator.ixx
        TerrainRenderer.ixx
        FxaaRenderer.ixx
        SsaoRenderer.ixx
)

set_target_properties(supertask PROPERTIES
        CXX_STANDARD 20
        CXX_SCAN_FOR_MODULES ON)

target_sources(supertask
        PUBLIC FILE_SET CXX_MODULES FILES Engine.ixx Graphics.ixx TerrainGenerator.ixx TerrainRenderer.ixx FxaaRenderer.ixx SsaoRenderer.ixx)

target_link_libraries(supertask
        PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(supertask
  shaders/terrain.vert
  shaders/terrain.tesc
  shaders/terrain.tese
  shaders/terrain.frag
  shaders/clipmap_splat.vert
  shaders/clipmap_splat.frag
  shaders/fxaa.vert
  shaders/fxaa.frag
  shaders/ssao.frag
  shaders/ssao_blur.frag
  shaders/ssao_apply.frag
)
