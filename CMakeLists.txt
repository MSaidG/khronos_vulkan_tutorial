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


add_executable(HelloTriangleApplication base_code.cpp)

target_include_directories(HelloTriangleApplication PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(HelloTriangleApplication PRIVATE ${GLFW_LIBRARIES} Vulkan::Vulkan)

function (add_slang_shader_target TARGET)
  cmake_parse_arguments ("SHADER" "" "" "SOURCES" ${ARGN})
  set (SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR}/shaders)
  set (ENTRY_POINTS -entry vertMain -entry fragMain)
  add_custom_command (
          OUTPUT ${SHADERS_DIR}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
  )
  add_custom_command (
          OUTPUT  ${SHADERS_DIR}/slang.spv
          COMMAND ${SLANGC_EXECUTABLE} ${SHADER_SOURCES} -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name ${ENTRY_POINTS} -o slang.spv
          WORKING_DIRECTORY ${SHADERS_DIR}
          DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
          COMMENT "Compiling Slang Shaders"
          VERBATIM
  )
  add_custom_target (${TARGET} DEPENDS ${SHADERS_DIR}/slang.spv)
endfunction()

# add_slang_shader_target( foo SOURCES ${SHADER_SLANG_SOURCES})
# add_dependencies(HelloTriangleApplication foo)