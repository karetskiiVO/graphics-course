#include "Shader.hpp"

#include <etna/Etna.hpp>

void Shader::Create (
    const std::string& name,
    std::initializer_list<std::filesystem::path> shaderPaths)
{
    programName = name;
    etna::create_program(programName.c_str(), shaderPaths);
}
