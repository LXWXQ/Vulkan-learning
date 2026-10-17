cmake_minimum_required(VERSION 3.15)
project(VulkanEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

# Vulkan SDK 通过 find_package 定位，不把某一台机器的安装路径写死。
# 如果 CMake 找不到，可设置环境变量 VULKAN_SDK，或传 -DVulkan_ROOT=<SDK 根目录>。
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glfw")
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glm")
set(TINYOBJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tinyobjloader")
set(VMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/VulkanMemoryAllocator")
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/stb")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/imgui")
set(TINYGLTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tinygltf")
set(ENTT_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/entt")
set(SPDLOG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/spdlog")

# tinygltf 是当前唯一必须存在、但体积较大且不进版本库的依赖。
# 提前给出可执行的修复命令，比在编译期报 "cannot open tiny_gltf.h" 清楚。
if(NOT EXISTS "${TINYGLTF_DIR}/tiny_gltf.h" OR NOT EXISTS "${TINYGLTF_DIR}/json.hpp")
    message(FATAL_ERROR
        "缺少 tinygltf/json.hpp。请执行：\n"
        "  git clone --depth 1 https://github.com/syoyo/tinygltf.git \"${TINYGLTF_DIR}\"")
endif()

# EnTT 用 src/ 作为 include 根，代码里写 #include <entt/entt.hpp>。
if(NOT EXISTS "${ENTT_DIR}/src/entt/entt.hpp")
    message(FATAL_ERROR
        "缺少 EnTT。请下载 v3.16.0（C++17 兼容）并放到 \"${ENTT_DIR}\"。")
endif()

# GLFW：优先使用 3rdParty/glfw 自带源码，避免依赖机器上是否安装过 glfw3。
# 如果以后把 3rdParty/glfw 换成系统包，也仍然保留 find_package 回退路径。
if(EXISTS "${GLFW_DIR}/CMakeLists.txt")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${GLFW_DIR}" "${CMAKE_BINARY_DIR}/glfw")
else()
    find_package(glfw3 REQUIRED)
endif()
set(GLM_INCLUDE_DIR "${GLM_DIR}")

file(GLOB_RECURSE SOURCES "src/*.cpp")

list(APPEND SOURCES "main.cpp")

list(APPEND SOURCES 
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${Vulkan_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIR}
    "${GLFW_DIR}/include"
    "${TINYOBJ_DIR}"
    "${TINYGLTF_DIR}"
    "${ENTT_DIR}/src"
    "${SPDLOG_DIR}"
    "${STB_DIR}"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${VMA_DIR}"
    "src"
)

# ---------------------------------------------------------------------------
# 资源路径注入：在配置阶段把绝对路径写进生成的 AssetPaths.h，
# 让引擎不再依赖"工作目录恰好是 build/Debug"这个隐式约定。
# ---------------------------------------------------------------------------
set(SENTINEL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SENTINEL_GENERATED_DIR}")

# W1：配置文件也放在工程根下的 config/ 目录，理由和资源路径一样 ——
# 不能依赖工作目录。放工程根（而不是 build/）是为了让 Debug / Release
# 共享同一份用户设置，而且这份文件是要进版本库的（作为默认预设）。
set(SENTINEL_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config")
file(MAKE_DIRECTORY "${SENTINEL_CONFIG_DIR}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssetPaths.h.in"
    "${SENTINEL_GENERATED_DIR}/AssetPaths.h"
    @ONLY
)
target_include_directories(${PROJECT_NAME} PRIVATE "${SENTINEL_GENERATED_DIR}")

# tinygltf 内部默认会展开一份 stb_image 实现；
# 而 Application.cpp 里已经定义了 STB_IMAGE_IMPLEMENTATION，两者会重复定义 stbi_* 符号。
# 因此关闭它自带的图片解码，改由我们注册自定义解码器（见 GLTFLoader.cpp）。
# 这两个宏必须对所有包含 tiny_gltf.h 的翻译单元一致，所以放在这里统一注入。
target_compile_definitions(${PROJECT_NAME} PRIVATE
    TINYGLTF_NO_STB_IMAGE
    TINYGLTF_NO_STB_IMAGE_WRITE
)

# ---------------------------------------------------------------------------
# VMA 泄漏排查开关。
#
# 打开之后：
#   1) VMA 在销毁分配器时把每条未释放的分配逐条打出来
#      （默认只有一句断言，不说是谁）；
#   2) sentinel_vmaNameFlag() 会给分配附上名字，于是日志里直接显示
#      "Name: SSAOUbo" 而不是 "Name: vma_empty"。
#
# 默认关：它会记录每次分配的调用点字符串，Debug 下明显变慢，而且它报告的
# 内容本身就意味着"代码有 bug"，不该是常态。
#   cmake -S . -B build -DSENTINEL_VMA_LEAK_CHECK=ON
# ---------------------------------------------------------------------------
option(SENTINEL_VMA_LEAK_CHECK "打开 VMA 的详细泄漏日志（排查用，会变慢）" OFF)
if(SENTINEL_VMA_LEAK_CHECK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SENTINEL_VMA_LEAK_CHECK)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan 
    glfw
    Threads::Threads
)

# shader 编译器同样由 Vulkan SDK 的 CMake 包给出，避免第二处硬编码路径。
set(GLSL_VALIDATOR "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
if(NOT GLSL_VALIDATOR OR NOT EXISTS "${GLSL_VALIDATOR}")
    find_program(GLSL_VALIDATOR NAMES glslangValidator glslangValidator.exe)
endif()
if(NOT GLSL_VALIDATOR)
    message(FATAL_ERROR "找不到 glslangValidator。请安装 Vulkan SDK，或传 -DGLSL_VALIDATOR=<路径>。")
endif()

set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OUTPUT_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY ${OUTPUT_SHADER_DIR})

file(GLOB SHADER_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.vert" "${SHADER_DIR}/*.frag" "${SHADER_DIR}/*.comp")
foreach(SHADER_FILE ${SHADER_FILES})
  
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
    
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${GLSL_VALIDATOR} -V "${SHADER_FILE}" -o "${OUTPUT_SHADER_DIR}/${FILE_NAME}.spv"
        COMMENT "自动编译 Shader: ${FILE_NAME} -> ${FILE_NAME}.spv"
    )
endforeach()