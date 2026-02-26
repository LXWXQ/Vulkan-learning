cmake_minimum_required(VERSION 3.15)
project(Vulkan2038_Sentinel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. 修正 Vulkan 路径 (注意：全部改用正斜杠 /)
# 哨兵提醒：你的 %VULKAN_SDK% 指向 Bin，但头文件通常在上一级
set(VULKAN_ROOT "D:/work/vulkan") 
set(Vulkan_INCLUDE_DIR "${VULKAN_ROOT}/Include")
set(Vulkan_LIBRARY "${VULKAN_ROOT}/Lib/vulkan-1.lib")

# 强制引导 CMake 寻找
find_package(Vulkan REQUIRED)

# 2. 修正 GLFW 和 GLM 路径 (同样改用正斜杠 /)
set(GLFW_DIR "C:/Users/Admin/Desktop/Vs code Demo/3rdParty/glfw")
set(GLM_DIR "C:/Users/Admin/Desktop/Vs code Demo/3rdParty/glm")

# 3. 寻找依赖
find_package(glfw3 REQUIRED)
find_path(GLM_INCLUDE_DIR "glm/glm.hpp" PATHS "${GLM_DIR}" NO_DEFAULT_PATH)

# 4. 扫描源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "main.cpp")

# 5. 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 6. 核心包含路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIR}
    "${GLFW_DIR}/include"
)

# 7. 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Vulkan::Vulkan 
    glfw
)

# 8. 自动拷贝 Shader
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMENT "正在同步 Shader 到运行目录。"
    )
endif()