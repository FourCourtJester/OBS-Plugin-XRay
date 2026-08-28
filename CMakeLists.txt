cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

# XRay is a dock plugin: both are required, not optional.
option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

# Off by default, and deliberately not wired into CI. The harness supplies its
# own libobs symbols, which is fine where they are plain functions but is at
# best warning-prone under MSVC, where obs.h marks them __declspec(dllexport)
# unconditionally. Untested there, so it stays opt-in rather than risking the
# Windows build.
option(ENABLE_TESTS "Build the SceneWalk test harness" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/SceneTransform.cpp
    src/SceneWalk.cpp
    src/SceneWalk.hpp
    src/XRayAddSource.cpp
    src/XRayAddSource.hpp
    src/XRayDock.cpp
    src/XRayDock.hpp
    src/XRayList.cpp
    src/XRayList.hpp
    src/XRayRow.cpp
    src/XRayRow.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(ENABLE_TESTS)
  enable_testing()

  add_executable(test-scene-walk tests/test_scene_walk.cpp src/SceneWalk.cpp)

  # Headers only. The harness defines the libobs entry points SceneWalk uses, so
  # linking the real library would collide with every one of them.
  target_include_directories(
    test-scene-walk
    PRIVATE
      src
      $<TARGET_PROPERTY:OBS::libobs,INTERFACE_INCLUDE_DIRECTORIES>
      $<TARGET_PROPERTY:OBS::obs-frontend-api,INTERFACE_INCLUDE_DIRECTORIES>
  )

  # The stub definitions do not repeat obs.h's dllexport annotation.
  set_target_properties(test-scene-walk PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

  add_test(NAME scene-walk COMMAND test-scene-walk)
endif()
