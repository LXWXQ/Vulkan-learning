#include "src/FirstApp.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>


#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <iomanip>

int main()
{
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif
    FirstApp app;
    try 
    {
        app.run();
    } 
    catch (const std::exception& e) 
    {
        std::cerr << "致命错误: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}