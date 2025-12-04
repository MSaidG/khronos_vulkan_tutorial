cmake_minimum_required(VERSION 3.10)

project(HelloTriangleApplication)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

if (NOT Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator not found in Vulkan SDK")
endif()

message(STATUS "Using glslangValidator: ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")


find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

set(SLANGC_EXECUTABLE "/home/msgulberk/Tools/Sources/vulkanSDK/1.4.328.1/x86_64/bin/slangc")

# add_compile_options(-O3 -march=native)

add_executable(HelloTriangleApplication base_code.cpp)

target_include_directories(HelloTriangleApplication PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(HelloTriangleApplication PRIVATE ${GLFW_LIBRARIES} Vulkan::Vulkan)

include_directories($ENV{VULKAN_SDK}/include)

# SLANG SHADER COMPILATION
function(add_slang_shader TARGET SOURCE_FILE OUTPUT_FILE)
    set(SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E env
            LD_LIBRARY_PATH=/home/msgulberk/Tools/Sources/vulkanSDK/1.4.328.1/x86_64/lib:$ENV{LD_LIBRARY_PATH}
            ${SLANGC_EXECUTABLE}
                ${SOURCE_FILE}
                -target spirv
                -profile spirv_1_4
                -emit-spirv-directly
                -fvk-use-entrypoint-name
                -entry vertMain
                -entry fragMain
                -o ${OUTPUT_FILE}
        WORKING_DIRECTORY ${SHADERS_DIR}
        DEPENDS ${SOURCE_FILE}
        COMMENT "Compiling Slang Shader ${SOURCE_FILE}"
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS ${OUTPUT_FILE})
endfunction()

set(SHADER_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)

# set(SHADER_SOURCE ${CMAKE_CURRENT_LIST_DIR}/shaders/shader.slang)
# set(SHADER_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/shaders/slang.spv)

add_slang_shader(compile_slang_shaders ${SHADER_DIR}/shader.slang ${SHADER_DIR}/slang.spv)



# GLSL SHADER COMPILATION
function(add_glsl_shader TARGET SOURCE_FILE OUTPUT_FILE)
    get_filename_component(SOURCE_DIR ${SOURCE_FILE} DIRECTORY)

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
            -V ${SOURCE_FILE}
            -o ${OUTPUT_FILE}
        DEPENDS ${SOURCE_FILE}
        WORKING_DIRECTORY ${SOURCE_DIR}
        COMMENT "Compiling GLSL shader ${SOURCE_FILE}"
        VERBATIM
    )

    add_custom_target(${TARGET} DEPENDS ${OUTPUT_FILE})
endfunction()



file(MAKE_DIRECTORY ${SHADER_DIR})

set(VERT_IN  ${SHADER_DIR}/27_shader_depth.vert)
set(FRAG_IN  ${SHADER_DIR}/27_shader_depth.frag)

set(VERT_OUT ${SHADER_DIR}/vert.spv)
set(FRAG_OUT ${SHADER_DIR}/frag.spv)

add_glsl_shader(compile_vert_shader ${VERT_IN} ${VERT_OUT})
add_glsl_shader(compile_frag_shader ${FRAG_IN} ${FRAG_OUT})

add_custom_target(compile_glsl_shaders ALL
    DEPENDS
        ${VERT_OUT}
        ${FRAG_OUT}
)



add_dependencies(HelloTriangleApplication compile_glsl_shaders compile_slang_shaders)
