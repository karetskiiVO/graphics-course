#include "RockScatter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/ext.hpp>


namespace {

uint32_t Hash(uint32_t value) {
    value = (value ^ (value >> 16)) * 0x7feb352du;
    value = (value ^ (value >> 15)) * 0x846ca68bu;
    return value ^ (value >> 16);
}

float Random(uint32_t cell, uint32_t lane) {
    return static_cast<float>(Hash(cell ^ (lane * 0x9e3779b9u)) >> 8) / 16777216.0f;
}

std::array<glm::vec4, 6> FrustumPlanes(const glm::mat4& matrix) {
    const auto rows = glm::transpose(matrix);
    std::array planes{
        rows[3] + rows[0], rows[3] - rows[0], rows[3] + rows[1],
        rows[3] - rows[1], rows[2], rows[3] - rows[2]
    };
    for (auto& plane : planes) plane /= glm::length(glm::vec3(plane));
    return planes;
}

bool Intersects(const std::array<glm::vec4, 6>& planes, glm::vec3 center, float radius) {
    for (const auto& plane : planes) {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
    }
    return true;
}

}

RockScatter::RockScatter(std::vector<float> values, uint32_t resolution, float extent, float scale)
    : heights{std::move(values)}
    , size{resolution}
    , worldSize{extent}
    , heightScale{scale}
{
    if (size < 2 || heights.size() != static_cast<std::size_t>(size) * size || worldSize <= 0) {
        throw std::invalid_argument("Invalid rock placement heightmap");
    }
}

float RockScatter::SampleHeight(glm::vec2 position) const {
    const auto texel = glm::clamp(position / worldSize, glm::vec2{0}, glm::vec2{1}) * static_cast<float>(size - 1);
    const auto x = static_cast<uint32_t>(texel.x);
    const auto z = static_cast<uint32_t>(texel.y);
    const auto nextX = std::min(x + 1, size - 1);
    const auto nextZ = std::min(z + 1, size - 1);
    const float a = glm::mix(heights[z * size + x], heights[z * size + nextX], texel.x - x);
    const float b = glm::mix(heights[nextZ * size + x], heights[nextZ * size + nextX], texel.x - x);
    return glm::mix(a, b, texel.y - z) * heightScale;
}

bool RockScatter::IsVisible(const glm::mat4& viewProjection, glm::vec3 center, float radius) {
    return Intersects(FrustumPlanes(viewProjection), center, radius);
}

std::array<std::vector<RockInstance>, 2> RockScatter::Generate(
    glm::vec3 camera, const glm::mat4& viewProjection,
    const RockSettings& settings, const std::array<RockBounds, 2>& bounds
) const {
    std::array<std::vector<RockInstance>, 2> result;
    if (settings.density <= 0.0f || settings.drawDistance <= 0.0f) return result;

    constexpr float spacing = 6.0f;
    const auto planes = FrustumPlanes(viewProjection);
    const int gridSize = static_cast<int>(std::ceil(worldSize / spacing));

    const float margin = settings.maxSize * std::max(
        bounds[0].radius + glm::length(bounds[0].center),
        bounds[1].radius + glm::length(bounds[1].center)
    );
    const float range = settings.drawDistance + margin;
    const int minX = std::max(0, static_cast<int>(std::floor((camera.x - range) / spacing)));
    const int minZ = std::max(0, static_cast<int>(std::floor((camera.z - range) / spacing)));
    const int maxX = std::min(gridSize - 1, static_cast<int>(std::floor((camera.x + range) / spacing)));
    const int maxZ = std::min(gridSize - 1, static_cast<int>(std::floor((camera.z + range) / spacing)));

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const uint32_t cell = Hash(static_cast<uint32_t>(x) ^ (static_cast<uint32_t>(z) * 0x85ebca6bu));
            const glm::vec2 position{(x + Random(cell, 1)) * spacing, (z + Random(cell, 2)) * spacing};
            if (position.x > worldSize || position.y > worldSize) continue;

            const float height = SampleHeight(position);
            const float weight = glm::smoothstep(20.0f, 45.0f, height) * (1.0f - glm::smoothstep(110.0f, 145.0f, height));
            if (Random(cell, 3) >= settings.density * weight) continue;

            const auto model = Hash(cell) & 1u;
            const float scale = glm::mix(settings.minSize, settings.maxSize, Random(cell, 4));
            const glm::vec3 normal = glm::normalize(glm::vec3{
                SampleHeight(position - glm::vec2{1, 0}) - SampleHeight(position + glm::vec2{1, 0}),
                2.0f,
                SampleHeight(position - glm::vec2{0, 1}) - SampleHeight(position + glm::vec2{0, 1}),
            });
            const auto tilt = glm::normalize(glm::quat{1.0f + normal.y, normal.z, 0.0f, -normal.x});
            const auto rotation = tilt * glm::angleAxis(Random(cell, 5) * glm::two_pi<float>(), glm::vec3{0, 1, 0});
            const auto transform = glm::translate(glm::mat4{1}, glm::vec3{position.x, height - scale * 0.08f, position.y})
                * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1}, glm::vec3{scale});
            const auto center = glm::vec3(transform * glm::vec4(bounds[model].center, 1));
            const float radius = bounds[model].radius * scale;
            if (glm::distance(center, camera) > settings.drawDistance + radius || !Intersects(planes, center, radius)) continue;

            const float tint = glm::mix(0.8f, 1.15f, Random(cell, 6));
            result[model].push_back({transform, glm::vec4{tint, tint * glm::mix(0.95f, 1.05f, Random(cell, 7)), tint, 1}});
        }
    }
    return result;
}
