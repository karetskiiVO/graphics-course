#pragma once
#ifndef ETNA_ETNA_ENGINE_CONFIG_HPP_INCLUDED
#define ETNA_ETNA_ENGINE_CONFIG_HPP_INCLUDED

#include <etna/Vulkan.hpp>

namespace etna
{

inline constexpr const char* ETNA_ENGINE_NAME = "etna";
inline constexpr uint32_t ETNA_ENGINE_VERSION = vk::makeApiVersion(0, 1, 10, 1);

} // namespace etna

#endif // ETNA_ETNA_ENGINE_CONFIG_HPP_INCLUDED