#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>

class Shader {
public:
    Shader () = default;

    void Create (const std::string& name, std::initializer_list<std::filesystem::path> shader_paths);

    const std::string& Name () const { return programName; }

    bool Valid () const { return !programName.empty(); }

private:
    std::string programName;
};
