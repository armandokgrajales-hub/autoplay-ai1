cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Solo forzar arm64 en macOS nativo, NO en Android ni Windows
if(APPLE AND NOT CMAKE_CROSSCOMPILING)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()

project(AutoPlayAI VERSION 1.0.0)

# Geode SDK - el action de GitHub lo configura automáticamente
if(NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK no está definido. En local: instala Geode CLI. En GitHub Actions: usa geode-sdk/build-geode-mod.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Crea la librería del mod
add_library(${PROJECT_NAME} SHARED
    src/main.cpp
)

# Linkea con Geode SDK
target_link_libraries(${PROJECT_NAME} geode-sdk)

# Configuración de Geode
setup_geode_mod(${PROJECT_NAME})

