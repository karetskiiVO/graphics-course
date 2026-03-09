#include "Shader.hpp"
#include <etna/Etna.hpp>

void Shader::Create (const std::string& name, std::initializer_list<std::filesystem::path> shader_paths) {
    programName = name;
    etna::create_program(programName.c_str(), shader_paths);
}
