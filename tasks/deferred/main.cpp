#include "App.hpp"

#include <cstdlib>
#include <exception>
#include <string_view>

#include <etna/Etna.hpp>
#include <spdlog/spdlog.h>


int main(int argc, char** argv) {
    spdlog::flush_on(spdlog::level::err);
    int result = EXIT_SUCCESS;
    try {
        const bool benchmark = argc == 2 && std::string_view(argv[1]) == "--benchmark";
        App(benchmark).Run();
    } catch (const std::exception& error) {
        spdlog::error("deferred: {}", error.what());
        result = EXIT_FAILURE;
    }

    if (etna::is_initilized()) etna::shutdown();

    return result;
}
