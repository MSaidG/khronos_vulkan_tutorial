cmake_minimum_required(VERSION 3.10)

project(HelloTriangleApplication)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# set(VULKAN_SDK "~/Tools/Sources/vulkanSDK/1.4.328.1")
# include_directories(${VULKAN_SDK}/x86_64/include)
# link_directories(${VULKAN_SDK}/x86_64/lib)
set(SLANGC_EXECUTABLE "/home/msgulberk/Tools/Sources/vulkanSDK/1.4.328.1/x86_64/bin/slangc")



add_executable(HelloTriangleApplication base_code.cpp)

target_include_directories(HelloTriangleApplication PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(HelloTriangleApplication PRIVATE ${GLFW_LIBRARIES} Vulkan::Vulkan)

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

set(SHADER_SOURCE ${CMAKE_CURRENT_LIST_DIR}/shaders/shader.slang)
set(SHADER_OUTPUT ${CMAKE_CURRENT_LIST_DIR}/shaders/slang.spv)

add_slang_shader(compile_shaders ${SHADER_SOURCE} ${SHADER_OUTPUT})
add_dependencies(HelloTriangleApplication compile_shaders)


# add_slang_shader_target( foo SOURCES ${SHADER_SLANG_SOURCES})
# add_dependencies(HelloTriangleApplication foo)