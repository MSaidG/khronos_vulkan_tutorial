# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

# Sets the minimum CMake version required for this project.
cmake_minimum_required(VERSION 3.22.1)

# Set C++ standard to match the main project
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Declares the project name. The project name can be accessed via ${ PROJECT_NAME},
# Since this is the top level CMakeLists.txt, the project name is also accessible
# with ${CMAKE_PROJECT_NAME} (both CMake variables are in-sync within the top level
# build script scope).
project("mobile")

set(MAIN_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../CMakeLists.txt")


find_package(Vulkan REQUIRED)
# Add the packages from the Android Game SDK
find_package(games-frame-pacing REQUIRED CONFIG)
find_package(games-performance-tuner REQUIRED CONFIG)
find_package(game-activity REQUIRED CONFIG)
include_directories(${ANDROID_NDK}/sources/android/game-activity/include)


# Set up shader compilation tools
add_executable(glslang::validator IMPORTED)
find_program(GLSLANG_VALIDATOR "glslangValidator" HINTS $ENV{VULKAN_SDK}/bin REQUIRED)
set_property(TARGET glslang::validator PROPERTY IMPORTED_LOCATION "${GLSLANG_VALIDATOR}")

# Define shader building function
function(add_shaders_target TARGET)
    cmake_parse_arguments("SHADER" "" "CHAPTER_NAME" "SOURCES" ${ARGN})
    set(SHADERS_DIR ${SHADER_CHAPTER_NAME}/shaders)
    add_custom_command(
            OUTPUT ${SHADERS_DIR}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADERS_DIR}
    )
    add_custom_command(
            OUTPUT ${SHADERS_DIR}/frag.spv ${SHADERS_DIR}/vert.spv
            COMMAND glslang::validator
            ARGS --target-env vulkan1.0 ${SHADER_SOURCES} --quiet
            WORKING_DIRECTORY ${SHADERS_DIR}
            DEPENDS ${SHADERS_DIR} ${SHADER_SOURCES}
            COMMENT "Compiling Shaders"
            VERBATIM
    )
    add_custom_target(${TARGET} DEPENDS ${SHADERS_DIR}/frag.spv ${SHADERS_DIR}/vert.spv)
endfunction()


# (Optional) Add the Vulkan C++ module when ENABLE_CPP20_MODULE=ON
add_library(VulkanCppModule SHARED)
target_compile_definitions(VulkanCppModule
        PUBLIC VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(VulkanCppModule
        PRIVATE
        "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(VulkanCppModule
        PUBLIC
        ${Vulkan_LIBRARIES}
)
set_target_properties(VulkanCppModule PROPERTIES CXX_STANDARD 20)

# Set up the C++ module file set
target_sources(VulkanCppModule
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
        "${Vulkan_INCLUDE_DIR}"
        FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
)


# Set up shader compilation for base_code.cpp
set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

# Copy shader source files to the build directory
configure_file(
        "${SHADER_SOURCE_DIR}/27_shader_depth.frag"
        "${SHADER_OUTPUT_DIR}/27_shader_depth.frag"
        COPYONLY
)
configure_file(
        "${SHADER_SOURCE_DIR}/27_shader_depth.vert"
        "${SHADER_OUTPUT_DIR}/27_shader_depth.vert"
        COPYONLY
)

# Compile shaders
set(SHADER_SOURCES "${SHADER_OUTPUT_DIR}/27_shader_depth.frag" "${SHADER_OUTPUT_DIR}/27_shader_depth.vert")
add_shaders_target(android_shaders CHAPTER_NAME "${SHADER_OUTPUT_DIR}" SOURCES ${SHADER_SOURCES})

# Creates and names a library, sets it as either STATIC
# or SHARED, and provides the relative paths to its source code.
# You can define multiple libraries, and CMake builds them for you.
# Gradle automatically packages shared libraries with your APK.
#
# In this top level CMakeLists.txt, ${CMAKE_PROJECT_NAME} is used to define
# the target library name; in the sub-module's CMakeLists.txt, ${PROJECT_NAME}
# is preferred for the same purpose.
#
# In order to load a library into your app from Java/Kotlin, you must call
# System.loadLibrary() and pass the name of the library defined here;
# for GameActivity/NativeActivity derived applications, the same library name must be
# used in the AndroidManifest.xml file.
add_library(${CMAKE_PROJECT_NAME} SHARED
        # List C/C++ source files with relative paths to this CMakeLists.txt.
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../base_code.cpp
        game_activity_bridge.cpp)

# Add dependency on shader compilation
add_dependencies(vulkan_tutorial_android android_shaders)


# Set include directories
target_include_directories(vulkan_tutorial_android PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${Vulkan_INCLUDE_DIR}
        ${ANDROID_NDK}/sources/android/game-activity/include
)


# Specifies libraries CMake should link to your target library. You
# can link libraries from various origins, such as libraries defined in this
# build script, prebuilt third-party libraries, or Android system libraries.
target_link_libraries(${CMAKE_PROJECT_NAME}
        # List libraries link to the target library
        android
        log
        games-frame-pacing::swappy_static
        games-performance-tuner::tuningfork_static
        game-activity::game-activity_static
        game-activity::game-activity
        VulkanCppModule
        ${Vulkan_LIBRARIES}
)