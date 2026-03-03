cmake_minimum_required(VERSION 3.15)
project(Vulkan2038_Sentinel)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
# 1. 修正 Vulkan 路径 (注意：全部改用正斜杠 /)
# 哨兵提醒：你的 %VULKAN_SDK% 指向 Bin，但头文件通常在上一级
set(MY_VULKAN_DIR "D:/work/vulkan") 
set(Vulkan_INCLUDE_DIR "${MY_VULKAN_DIR}/Include")
set(Vulkan_LIBRARY "${MY_VULKAN_DIR}/Lib/vulkan-1.lib")

# 强制引导 CMake 寻找
find_package(Vulkan REQUIRED)

# 2. 修正 GLFW 和 GLM 路径 (同样改用正斜杠 /)
set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glfw")
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glm")
set(TINYOBJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tinyobjloader")
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/stb")
# 3. 寻找依赖
find_package(glfw3 REQUIRED)
set(GLM_INCLUDE_DIR "${GLM_DIR}")
# 4. 扫描源文件
file(GLOB_RECURSE SOURCES "src/*.cpp" "main.cpp")

# 5. 生成可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 6. 核心包含路径
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIR}
    "${GLFW_DIR}/include"
    "${TINYOBJ_DIR}"
    "${STB_DIR}"
)

# 7. 链接库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Vulkan::Vulkan 
    glfw
)

# 8. 自动拷贝 Shader
set(GLSL_VALIDATOR "D:/work/vulkan/Bin/glslangValidator.exe")
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OUTPUT_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# 确保输出目录存在
file(MAKE_DIRECTORY ${OUTPUT_SHADER_DIR})

# 扫描所有的 .vert 和 .frag 文件
file(GLOB SHADER_FILES "${SHADER_DIR}/*.vert" "${SHADER_DIR}/*.frag")

foreach(SHADER_FILE ${SHADER_FILES})
    # 获取文件名 (例如 shader.vert)
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
    
    # 告诉 CMake：在编译 C++ 之前，先执行 glslangValidator
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${GLSL_VALIDATOR} -V "${SHADER_FILE}" -o "${OUTPUT_SHADER_DIR}/${FILE_NAME}.spv"
        COMMENT "自动编译 Shader: ${FILE_NAME} -> ${FILE_NAME}.spv"
    )
endforeach()