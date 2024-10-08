# -*- cmake -*-

# cmake_minimum_required should appear before any
# other commands to guarantee full compatibility
# with the version specified
## 3.8 added VS_DEBUGGER_WORKING_DIRECTORY support
## 3.13/12 is needed for add_link_options/add_compile_definitions
## 3.14 added FILE CREATE_LINK
## 3.16 is needed for target_precompile_headers
## Nicky: Ideally we want at least 3.21 for good preset support
##   We're not there yet, but once done, there is a kludge in Linking.cmake
#    "if(${CMAKE_VERSION} VERSION_LESS "3.20.0")" that can also be removed
cmake_minimum_required(VERSION 3.16.0 FATAL_ERROR)
if(POLICY CMP0063)
  cmake_policy(SET CMP0063 NEW)
endif()
if(POLICY CMP0083)
  cmake_policy(SET CMP0083 NEW)
endif()
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0119)
  cmake_policy(SET CMP0119 NEW)
endif()

set(ROOT_PROJECT_NAME "Alchemy" CACHE STRING
    "The root project/makefile/solution name. Defaults to Alchemy.")
project(${ROOT_PROJECT_NAME})

# The viewer code base can now be successfully compiled with -std=c++14. But
# turning that on in the generic viewer-build-variables/variables file would
# potentially require tweaking each of our ~50 third-party library builds.
# Until we decide to set -std=c++14 in viewer-build-variables/variables, set
# it locally here: we want to at least prevent inadvertently reintroducing
# viewer code that would fail with C++14.
if(MSVC)
  set(CMAKE_CXX_STANDARD 20)
else()
  set(CMAKE_OBJCXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  enable_language(OBJCXX)
endif()

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake"  "${CMAKE_CURRENT_BINARY_DIR}")

include(conanbuildinfo OPTIONAL RESULT_VARIABLE USE_CONAN )
if( USE_CONAN )
    set( USE_CONAN ON )
    set( USE_AUTOBUILD_3P OFF )
    conan_basic_setup(TARGETS NO_OUTPUT_DIRS)
    add_compile_definitions(LL_USESYSTEMLIBS USE_CONAN NO_AUTOBUILD_3P)
else()
  set( USE_CONAN OFF )
  set( USE_AUTOBUILD_3P ON )
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Build type.  One of: Debug Release RelWithDebInfo" FORCE)
endif (NOT CMAKE_BUILD_TYPE)

set(Python3_FIND_VIRTUALENV FIRST)
find_package(Python3 COMPONENTS Interpreter)

include(Variables)
include(BuildVersion)

add_subdirectory(cmake)
add_subdirectory(deps)

add_subdirectory(${LIBS_OPEN_PREFIX}llaudio)
add_subdirectory(${LIBS_OPEN_PREFIX}llappearance)
add_subdirectory(${LIBS_OPEN_PREFIX}llcharacter)
add_subdirectory(${LIBS_OPEN_PREFIX}llcommon)
add_subdirectory(${LIBS_OPEN_PREFIX}llcorehttp)
add_subdirectory(${LIBS_OPEN_PREFIX}llimage)
add_subdirectory(${LIBS_OPEN_PREFIX}llkdu)
add_subdirectory(${LIBS_OPEN_PREFIX}llimagej2coj)
add_subdirectory(${LIBS_OPEN_PREFIX}llinventory)
add_subdirectory(${LIBS_OPEN_PREFIX}llmath)
add_subdirectory(${LIBS_OPEN_PREFIX}llmeshoptimizer)
add_subdirectory(${LIBS_OPEN_PREFIX}llmessage)
add_subdirectory(${LIBS_OPEN_PREFIX}llprimitive)
add_subdirectory(${LIBS_OPEN_PREFIX}llrender)
add_subdirectory(${LIBS_OPEN_PREFIX}llfilesystem)
add_subdirectory(${LIBS_OPEN_PREFIX}llwebrtc)
add_subdirectory(${LIBS_OPEN_PREFIX}llwindow)
add_subdirectory(${LIBS_OPEN_PREFIX}llxml)

if (WINDOWS AND EXISTS ${LIBS_CLOSED_DIR}copy_win_scripts)
  add_subdirectory(${LIBS_CLOSED_PREFIX}copy_win_scripts)
endif (WINDOWS AND EXISTS ${LIBS_CLOSED_DIR}copy_win_scripts)

add_custom_target(viewer)

add_subdirectory(${LIBS_OPEN_PREFIX}llplugin)
add_subdirectory(${LIBS_OPEN_PREFIX}llui)
add_subdirectory(${LIBS_OPEN_PREFIX}viewer_components)

if( LL_TESTS )
# Legacy C++ tests. Build always, run if LL_TESTS is true.
add_subdirectory(${VIEWER_PREFIX}test)
endif()

if (ENABLE_MEDIA_PLUGINS)
# viewer media plugins
add_subdirectory(${LIBS_OPEN_PREFIX}media_plugins)
endif (ENABLE_MEDIA_PLUGINS)

if (USE_BUGSPLAT)
    if (BUGSPLAT_DB)
        message(STATUS "Building with BugSplat; database '${BUGSPLAT_DB}'")
    else (BUGSPLAT_DB)
        message(WARNING "Building with BugSplat, but no database name set (BUGSPLAT_DB)")
    endif (BUGSPLAT_DB)
endif (USE_BUGSPLAT)

add_subdirectory(${VIEWER_PREFIX}newview)
add_dependencies(viewer alchemy-bin)

add_subdirectory(${VIEWER_PREFIX}doxygen EXCLUDE_FROM_ALL)

# sets the 'startup project' for debugging from visual studio.
set_property(
        DIRECTORY ${VIEWER_PREFIX}
        PROPERTY VS_STARTUP_PROJECT ${VIEWER_BINARY_NAME}
        )

if (LL_TESTS)
  # Define after the custom targets are created so
  # individual apps can add themselves as dependencies
  add_subdirectory(${INTEGRATION_TESTS_PREFIX}integration_tests)
endif (LL_TESTS)

