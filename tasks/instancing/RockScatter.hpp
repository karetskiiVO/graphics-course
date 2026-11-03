#pragma once

#include <array>
#include <vector>

#include <glm/glm.hpp>


struct RockInstance {
    glm::mat4 model;
    glm::vec4 color;
};

static_assert(sizeof(RockInstance) == 80);

struct RockBounds {
    glm::vec3 center;
    float radius;
};

struct RockSettings {
    float density = 0.35f;
    float drawDistance = 180.0f;
    float minSize = 0.8f;
    float maxSize = 3.0f;
};

class RockScatter {
public:
    RockScatter(std::vector<float> heights, uint32_t size, float worldSize, float heightScale);

    float SampleHeight(glm::vec2 position) const;
    static bool IsVisible(const glm::mat4& viewProjection, glm::vec3 center, float radius);
    std::array<std::vector<RockInstance>, 2> Generate(
        glm::vec3 camera, const glm::mat4& viewProjection,
        const RockSettings& settings, const std::array<RockBounds, 2>& bounds
    ) const;

private:
    std::vector<float> heights;
    uint32_t size;
    float worldSize;
    float heightScale;
};
