cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME Raytracer)
project(${PROJECT_NAME} 
        VERSION 0.1.0
        LANGUAGES CXX)

set ( CMAKE_CXX_STANDARD 20 )

## Asset handling removed to simplify configuration.
## This CMakeLists now focuses on building the project sources only.
## If you need to install or copy assets, add explicit install/copy rules.


set(RAYTRACER_CORE_HEADERS
    src/3rdParty/tinyxml2/tinyxml2.h
    src/3rdParty/ObjLoader/OBJ_Loader.h
    src/engine/aabb.h
    src/engine/bvh_node.h
    src/engine/camera.h
    src/engine/config.h
    src/engine/hittable.h
    src/engine/sphere.h
    src/engine/triangle.h
    src/engine/material.h
    src/engine/emissive.h
    src/engine/lambertian.h
    src/engine/metal.h
    src/engine/sun.h
    src/engine/mesh.h
    src/engine/hittable_list.h
    src/engine/world.h
    src/engine/render_runner.h
    src/engine/factories/factory_methods.h
    src/engine/perlin.h
    src/engine/noise_texture.h
    src/engine/quad.h
    src/engine/translate.h
    src/engine/rotate_y.h
    src/engine/isotropic.h
    src/engine/constant_medium.h
    src/engine/onb.h
    src/engine/pdf.h
    src/engine/scatter_record.h
    src/engine/ggx_material.h
    src/engine/mis.h
    src/util/vec3.h
    src/util/ray.h
    src/defs.h
)

set(RAYTRACER_CORE_SOURCES
    src/3rdParty/tinyxml2/tinyxml2.cpp
    src/engine/aabb.cpp
    src/engine/bvh_node.cpp
    src/engine/camera.cpp
    src/engine/config.cpp
    src/engine/shpere.cpp
    src/engine/triangle.cpp
    src/engine/lambertian.cpp
    src/engine/metal.cpp
    src/engine/emissive.cpp
    src/engine/dielectric.cpp
    src/engine/hdri_environment.cpp
    src/engine/oidn_denoiser.cpp
    src/engine/gltf_loader.cpp
    src/engine/sun.cpp
    src/engine/mesh.cpp
    src/engine/hittable_list.cpp
    src/engine/world.cpp
    src/engine/render_runner.cpp
    src/engine/factories/factory_methods.cpp
    src/util/vec3.cpp
    src/util/logging.cpp
    src/3rdParty/stb_image_write.cpp
    src/3rdParty/stb_image_impl.cpp
    src/engine/pdf.cpp
)

set(RAYTRACER_CLI_SOURCES
    src/main.cpp
)

## Try to find system-installed GLM; optionally build bundled GLM when requested
option(USE_BUNDLED_GLM "When ON, build the bundled GLM in src/3rdParty/glm-master if system GLM is not found" OFF)

find_package(glm CONFIG QUIET)
if(TARGET glm::glm)
    message(STATUS "Found system GLM (glm::glm)")
else()
    if(USE_BUNDLED_GLM)
        message(STATUS "Using bundled GLM subdirectory (USE_BUNDLED_GLM=ON)")
        add_subdirectory(src/3rdParty/glm-master ${CMAKE_BINARY_DIR}/glm)
    else()
        message(FATAL_ERROR "GLM not found. Install system GLM (e.g. libglm-dev) or re-run CMake with -DUSE_BUNDLED_GLM=ON to build the bundled copy.")
    endif()
endif()

# PNG support: try to find libpng and link it for real PNG output
find_package(PNG REQUIRED)

# OIDN support: Intel Open Image Denoise for AI denoising
# Install via: brew install open-image-denoise
find_package(OpenImageDenoise QUIET)
if(OpenImageDenoise_FOUND)
    message(STATUS "Found OIDN: ${OpenImageDenoise_VERSION}")
    set(USE_OIDN TRUE)
else()
    message(STATUS "OIDN not found - using fallback denoiser")
    set(USE_OIDN FALSE)
endif()

add_library(raytracer_core
        ${RAYTRACER_CORE_HEADERS}
        ${RAYTRACER_CORE_SOURCES}
)

target_include_directories(raytracer_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

# Some GLM features in gtx/* are experimental and require this define
# to be present before including those headers. Keep it on the core library
# so every consumer (CLI, UI, tests) inherits it automatically.
target_compile_definitions(raytracer_core PRIVATE GLM_ENABLE_EXPERIMENTAL)

# Add OIDN define if available
if(USE_OIDN)
    target_compile_definitions(raytracer_core PUBLIC USE_OIDN)
endif()

# Link libraries needed by the rendering core.
target_link_libraries(raytracer_core
    PUBLIC
        glm::glm
        PNG::PNG
)

# Link OIDN if available
if(USE_OIDN)
    target_link_libraries(raytracer_core PUBLIC OpenImageDenoise)
endif()

add_executable(${PROJECT_NAME}
        ${RAYTRACER_CLI_SOURCES}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        raytracer_core
)


# Install rules: binary + assets (source assets directory)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets DESTINATION share/${PROJECT_NAME})

# Convenience target: install into a staging directory under the build tree
add_custom_target(install-staging
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${CMAKE_BINARY_DIR}/staging
    COMMENT "Installing to staging directory: ${CMAKE_BINARY_DIR}/staging"
)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

# ==========================================
# UI Dependencies (GLFW + ImGui)
# ==========================================
# 1. GLFW
add_subdirectory(src/3rdParty/glfw)

# 2. Dear ImGui
# ImGui is header-only + sources, no CMakeLists by default usually, but we build our lib from it.
set(imgui_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/3rdParty/imgui")

# Define ImGui Library Target
# imgui doesn't provide a CMake target by default easily, so we build one.
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC 
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

# ==========================================
# RaytracerGUI Executable
# ==========================================
add_executable(RaytracerGUI
    src/gui/main.cpp
    src/gui/gui_application.cpp
    src/gui/gl_texture.cpp
)

target_include_directories(RaytracerGUI PRIVATE src)
target_link_libraries(RaytracerGUI
    PRIVATE
        raytracer_core
        imgui_lib
        glfw
)

# Mac/Linux specific linking
if(APPLE)
    target_link_libraries(RaytracerGUI PRIVATE "-framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo")
elseif(UNIX)
    target_link_libraries(RaytracerGUI PRIVATE GL)
endif()
