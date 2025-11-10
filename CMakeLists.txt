cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME Raytracer)
project(${PROJECT_NAME} 
        VERSION 0.1.0
        LANGUAGES CXX)

set ( CMAKE_CXX_STANDARD 20 )

CONFIGURE_FILE(assets/objects.xml objects.xml COPYONLY)

# Copy optional mesh files only if they exist in the source tree. This avoids
# failing configuration when some .obj assets are missing (e.g. not checked in).
set(_MESH_FILES
    box_stack.obj
    bed.obj
    chair.obj
    cube.obj
    monkey.obj
    sphere.obj
    stool.obj
    table.obj
    tree.obj
)

# Option to require mesh assets. Default: OFF (optional meshes).
# When ON, CMake will fail configuration if any mesh from _MESH_FILES is missing.
option(REQUIRE_MESHES "Fail configure if mesh assets are missing" OFF)
option(GENERATE_PLACEHOLDER_MESHES "When ON, missing mesh files will be auto-generated as minimal .obj placeholders" OFF)

set(_missing_meshes)
set(_generated_dir "${CMAKE_BINARY_DIR}/generated_meshes")
foreach(_mesh IN LISTS _MESH_FILES)
    set(_src "${CMAKE_SOURCE_DIR}/assets/mesh/${_mesh}")
    if(EXISTS ${_src})
        message(STATUS "Copying mesh: ${_mesh}")
        get_filename_component(_name ${_mesh} NAME)
        configure_file(${CMAKE_SOURCE_DIR}/assets/mesh/${_mesh} ${_name} COPYONLY)
    else()
        if(GENERATE_PLACEHOLDER_MESHES)
            # Ensure generated dir exists
            file(MAKE_DIRECTORY ${_generated_dir})
            set(_gen_path "${_generated_dir}/${_mesh}")
            if(NOT EXISTS ${_gen_path})
                message(STATUS "Generating placeholder mesh: ${_mesh}")
                file(WRITE ${_gen_path} "# Minimal placeholder OBJ for ${_mesh}\n")
                file(APPEND ${_gen_path} "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n")
            endif()
            get_filename_component(_name ${_mesh} NAME)
            configure_file(${_gen_path} ${_name} COPYONLY)
        else()
            if(REQUIRE_MESHES)
                list(APPEND _missing_meshes ${_mesh})
            else()
                message(VERBOSE "Mesh not found, skipping: ${_mesh}")
            endif()
        endif()
    endif()
endforeach()

if(REQUIRE_MESHES AND _missing_meshes)
    message(FATAL_ERROR "The following required mesh files are missing in ${CMAKE_SOURCE_DIR}/assets/mesh/:\n  ${_missing_meshes}\nSet REQUIRE_MESHES=OFF or enable GENERATE_PLACEHOLDER_MESHES to configure without these assets.")
endif()

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

# Executable
add_executable(${PROJECT_NAME}
                ${HDR} 
                ${SRC} 
            )

# Link libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        glm::glm
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
