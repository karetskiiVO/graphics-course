add_executable(supertask
        main.cpp
)

set_target_properties(supertask PROPERTIES
        CXX_STANDARD 20
        CXX_SCAN_FOR_MODULES ON)

target_sources(supertask
        PUBLIC FILE_SET CXX_MODULES FILES RenderCore.ixx)

target_link_libraries(supertask
        PRIVATE glfw etna glm::glm wsi gui scene render_utils)

# target_add_shaders(supertask)
