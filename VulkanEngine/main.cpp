#include "src/FirstApp.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    FirstApp app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}