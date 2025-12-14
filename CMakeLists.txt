cmake_minimum_required(VERSION 3.16)

set(PROJECT_NAME Raytracer)
project(${PROJECT_NAME} 
        VERSION 0.1.0
        LANGUAGES CXX)

set ( CMAKE_CXX_STANDARD 20 )

## Asset handling removed to simplify configuration.
## This CMakeLists now focuses on building the project sources only.
## If you need to install or copy assets, add explicit install/copy rules.

option(BUILD_QT_UI "Build the Qt6-based graphical UI" OFF)

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

if(BUILD_QT_UI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    find_package(Qt6 6.5 COMPONENTS Widgets Gui Concurrent Network Xml OpenGLWidgets REQUIRED)

    set(RAYTRACER_UI_HEADERS
            src/ui/views/MainWindow.h
            src/ui/views/RenderSettingsPanel.h
            src/ui/views/RenderViewportWidget.h
            src/ui/models/RenderSettingsModel.h
            src/ui/models/RenderQueueModel.h
            src/ui/models/PresetRepository.h
            src/ui/controllers/RenderController.h
            src/ui/services/RendererService.h
            src/ui/services/SceneImportService.h
            src/ui/services/remote/RemoteWorkerConfig.h
            src/ui/services/remote/RemoteCredentialsStore.h
            src/ui/services/remote/RemoteDispatchCommon.h
            src/ui/services/remote/RemoteDispatchClient.h
            src/ui/services/remote/SshDispatchClient.h
            src/ui/services/remote/HttpDispatchClient.h
            src/ui/services/remote/RemoteDispatchManager.h
            src/ui/views/PresetEditorDialog.h
            src/ui/views/RemoteCredentialDialog.h
            src/ui/rendering/ToneMappingPresets.h
            # Scene Editor - Models
            src/ui/models/Transform.h
            src/ui/models/MaterialDefinition.h
            src/ui/models/SceneNode.h
            src/ui/models/SceneDocument.h
            src/ui/models/DesignSpaceFactory.h
            # Scene Editor - Commands
            src/ui/commands/Command.h
            src/ui/commands/CommandHistory.h
            src/ui/commands/SceneCommands.h
            # Scene Editor - Controllers
            src/ui/controllers/ViewportCameraController.h
            src/ui/controllers/SelectionManager.h
            # Scene Editor - Views
            src/ui/views/SceneViewport.h
            src/ui/views/SceneHierarchyPanel.h
            src/ui/views/PropertiesPanel.h
            src/ui/views/SceneEditorWidget.h
            # Scene Editor - Services
            src/ui/services/SceneConverter.h
            # Scene Editor - Serialization
            src/ui/serialization/SceneSerializer.h
            src/ui/serialization/XMLSceneSerializer.h
    )

    set(RAYTRACER_UI_SOURCES
            src/ui/main.cpp
            src/ui/views/MainWindow.cpp
            src/ui/views/RenderSettingsPanel.cpp
            src/ui/views/RenderViewportWidget.cpp
            src/ui/models/RenderSettingsModel.cpp
            src/ui/models/RenderQueueModel.cpp
            src/ui/models/PresetRepository.cpp
            src/ui/controllers/RenderController.cpp
            src/ui/services/RendererService.cpp
            src/ui/services/SceneImportService.cpp
            src/ui/services/remote/RemoteWorkerConfig.cpp
            src/ui/services/remote/RemoteCredentialsStore.cpp
            src/ui/services/remote/SshDispatchClient.cpp
            src/ui/services/remote/HttpDispatchClient.cpp
            src/ui/services/remote/RemoteDispatchManager.cpp
            src/ui/views/PresetEditorDialog.cpp
            src/ui/views/RemoteCredentialDialog.cpp
            src/ui/rendering/ToneMappingPresets.cpp
            # Scene Editor - Models
            src/ui/models/Transform.cpp
            src/ui/models/MaterialDefinition.cpp
            src/ui/models/SceneNode.cpp
            src/ui/models/SceneDocument.cpp
            src/ui/models/DesignSpaceFactory.cpp
            # Scene Editor - Commands
            src/ui/commands/CommandHistory.cpp
            src/ui/commands/SceneCommands.cpp
            # Scene Editor - Controllers
            src/ui/controllers/ViewportCameraController.cpp
            src/ui/controllers/SelectionManager.cpp
            # Scene Editor - Views
            src/ui/views/SceneViewport.cpp
            src/ui/views/SceneHierarchyPanel.cpp
            src/ui/views/PropertiesPanel.cpp
            src/ui/views/LightingPanel.cpp
            src/ui/views/SceneEditorWidget.cpp
            # Scene Editor - Services
            src/ui/services/SceneConverter.cpp
            # Scene Editor - Serialization
            src/ui/serialization/SceneSerializer.cpp
            src/ui/serialization/XMLSceneSerializer.cpp
    )

    add_executable(RaytracerUI
            ${RAYTRACER_UI_HEADERS}
            ${RAYTRACER_UI_SOURCES}
    )

    target_link_libraries(RaytracerUI
        PRIVATE
            raytracer_core
            Qt6::Widgets
            Qt6::Gui
            Qt6::Concurrent
            Qt6::Network
            Qt6::Xml
            Qt6::OpenGLWidgets
    )

    if(APPLE)
        target_link_libraries(RaytracerUI PRIVATE "-framework Security")
    endif()

    target_include_directories(RaytracerUI PRIVATE ${CMAKE_SOURCE_DIR}/src)

    install(TARGETS RaytracerUI RUNTIME DESTINATION bin)
endif()

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
