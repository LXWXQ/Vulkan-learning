cmake_minimum_required(VERSION 3.15)
project(VulkanEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

set(MY_VULKAN_DIR "D:/work/vulkan") 
set(Vulkan_INCLUDE_DIR "${MY_VULKAN_DIR}/Include")
set(Vulkan_LIBRARY "${MY_VULKAN_DIR}/Lib/vulkan-1.lib")

find_package(Vulkan REQUIRED)

set(GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glfw")
set(GLM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/glm")
set(TINYOBJ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tinyobjloader")
set(STB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/stb")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/imgui")

find_package(glfw3 REQUIRED)
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
    "${STB_DIR}"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    Vulkan::Vulkan 
    glfw
)

set(GLSL_VALIDATOR "D:/work/vulkan/Bin/glslangValidator.exe")
set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(OUTPUT_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY ${OUTPUT_SHADER_DIR})

file(GLOB SHADER_FILES "${SHADER_DIR}/*.vert" "${SHADER_DIR}/*.frag")
file(GLOB SHADER_FILES CONFIGURE_DEPENDS "${SHADER_DIR}/*.vert" "${SHADER_DIR}/*.frag")
foreach(SHADER_FILE ${SHADER_FILES})
  
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
    
    add_custom_command(
        TARGET ${PROJECT_NAME} PRE_BUILD
        COMMAND ${GLSL_VALIDATOR} -V "${SHADER_FILE}" -o "${OUTPUT_SHADER_DIR}/${FILE_NAME}.spv"
        COMMENT "自动编译 Shader: ${FILE_NAME} -> ${FILE_NAME}.spv"
    )
endforeach()