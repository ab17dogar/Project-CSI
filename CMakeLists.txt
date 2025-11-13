cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME Raytracer)
project(${PROJECT_NAME} 
        VERSION 0.1.0
        LANGUAGES CXX)

set ( CMAKE_CXX_STANDARD 20 )

## Asset handling removed to simplify configuration.
## This CMakeLists now focuses on building the project sources only.
## If you need to install or copy assets, add explicit install/copy rules.

set ( HDR 
        src/3rdParty/tinyxml2/tinyxml2.h
        src/3rdParty/ObjLoader/OBJ_Loader.h
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
        src/engine/factories/factory_methods.h
        src/util/vec3.h
        src/util/ray.h
        src/defs.h
    )

set ( SRC 
        src/3rdParty/tinyxml2/tinyxml2.cpp
        src/engine/camera.cpp
        src/engine/config.cpp
        src/engine/shpere.cpp
        src/engine/triangle.cpp
        src/engine/lambertian.cpp
        src/engine/metal.cpp
        src/engine/emissive.cpp
        src/engine/sun.cpp
        src/engine/mesh.cpp
        src/engine/hittable_list.cpp
        src/engine/world.cpp
        src/engine/factories/factory_methods.cpp
        src/util/vec3.cpp
        src/main.cpp
        src/3rdParty/stb_image_write.cpp
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

# Executable
add_executable(${PROJECT_NAME}
                ${HDR} 
                ${SRC} 
            )

# Some GLM features in gtx/* are experimental and require this define
# to be present before including those headers. Add it for the target so
# system or bundled GLM usage works without modifying third-party headers.
target_compile_definitions(${PROJECT_NAME} PRIVATE GLM_ENABLE_EXPERIMENTAL)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glm::glm
    PNG::PNG
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
