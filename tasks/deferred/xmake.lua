target("deferred")
    set_kind("binary")
    set_target_layout("tasks/deferred")
    add_files("main.cpp", "App.cpp", "Decals.cpp", "RockModel.cpp", "RockScatter.cpp")
    add_deps("wsi", "gui", "etna")
    add_packages("glfw", "glm", "imgui", "tinygltf", {public = false})
    compile_shaders("shaders")
target_end()

