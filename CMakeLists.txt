# CMake Project for Refresh
# Written by @thatcosmonaut
cmake_minimum_required(VERSION 3.16)
project(Refresh C)

include(CheckCCompilerFlag)
include(CMakePushCheckState)

# Options
option(BUILD_SHARED_LIBS "Build shared library" ON)

# Version
SET(LIB_MAJOR_VERSION "2")
SET(LIB_MINOR_VERSION "0")
SET(LIB_REVISION "0")
SET(LIB_VERSION "${LIB_MAJOR_VERSION}.${LIB_MINOR_VERSION}.${LIB_REVISION}")

# Build Type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	# By default, we use Release
	message(STATUS "Setting build type to 'Release' as none was specified.")
	set(CMAKE_BUILD_TYPE "Release" CACHE
		STRING "Choose the type of build." FORCE
	)

	# Set the possible values of build type for cmake-gui
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
		STRINGS "Debug" "Release" "RelWithDebInfo"
	)
endif()

# Platform Flags
if(APPLE)
	set(CMAKE_MACOSX_RPATH ON)
	set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
	set(LOBJC "objc")
elseif(WIN32)
	# "Refresh.dll", not "libRefresh.dll"
	set(CMAKE_SHARED_LIBRARY_PREFIX "")
endif()

# Defines
add_definitions(
	-DREFRESH_VULKAN
)

if (WIN32)
	add_definitions(
		-DREFRESH_D3D11
	)
endif()

if (APPLE)
	add_definitions(
		-DREFRESH_METAL
	)
endif()

# Source lists
set(Refresh_source
	# Public Headers
	include/Refresh.h
	include/Refresh_image.h
	# Internal Headers
	src/Refresh_driver.h
	src/Refresh_spirv_c.h
	src/spirv_cross_c.h
	src/vulkan/Refresh_vulkan_vkfuncs.h
	# Source Files
	src/Refresh.c
	src/Refresh_spirv.c
	src/d3d11/Refresh_d3d11.c
	src/vulkan/Refresh_vulkan.c
	src/Refresh_image.c
)

if (APPLE)
	set(Refresh_source ${Refresh_source} src/metal/Refresh_metal.m)
endif()

add_library(Refresh ${Refresh_source})

# Build flags
if(NOT MSVC)
	set_property(TARGET Refresh PROPERTY COMPILE_FLAGS "-std=gnu99 -Wall -Wno-strict-aliasing -pedantic")
endif()

# Windows is silly and we need to manually include the Vulkan SDK
if(MSVC)
	target_include_directories(Refresh PUBLIC $ENV{VULKAN_SDK}/include)
endif()

# Refresh folders as includes, for other targets to consume
target_include_directories(Refresh PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Vulkan-Headers/include>
)

# MinGW builds should statically link libgcc
if(MINGW)
	target_link_libraries(Refresh PRIVATE -static-libgcc)
endif()

# ARC
if (APPLE)
	cmake_push_check_state(RESET)
	check_c_compiler_flag(-fobjc-arc COMPILER_SUPPORTS_FOBJC_ARC)
	cmake_pop_check_state()

	if(NOT COMPILER_SUPPORTS_FOBJC_ARC)
		message(FATAL_ERROR "Compiler does not support -fobc-arc: this is required on Apple")
	endif()
	set_property(TARGET Refresh PROPERTY COMPILE_FLAGS "-fobjc-arc")
endif()

# Link Foundation and Metal
if (APPLE)
	target_link_libraries(Refresh PRIVATE "-Wl,-framework,Foundation")
	target_link_libraries(Refresh PRIVATE "-Wl,-framework,CoreGraphics")
	if(IOS OR TVOS OR VISIONOS OR WATCHOS)
		target_link_libraries(Refresh PRIVATE "-Wl,-framework,Metal")
	else()
		target_link_libraries(Refresh PRIVATE "-Wl,-weak_framework,Metal")
	endif()
endif()

# Soname
set_target_properties(Refresh PROPERTIES OUTPUT_NAME "Refresh"
	VERSION ${LIB_VERSION}
	SOVERSION ${LIB_MAJOR_VERSION}
)

# SDL2 Dependency
if (DEFINED SDL2_INCLUDE_DIRS AND DEFINED SDL2_LIBRARIES)
	message(STATUS "using pre-defined SDL2 variables SDL2_INCLUDE_DIRS and SDL2_LIBRARIES")
	target_include_directories(Refresh PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
	target_link_libraries(Refresh PUBLIC ${SDL2_LIBRARIES})
else()
	# Only try to autodetect if both SDL2 variables aren't explicitly set
	find_package(SDL2 CONFIG)
	if (TARGET SDL2::SDL2)
		message(STATUS "using TARGET SDL2::SDL2")
		target_link_libraries(Refresh PUBLIC SDL2::SDL2)
	elseif (TARGET SDL2)
		message(STATUS "using TARGET SDL2")
		target_link_libraries(Refresh PUBLIC SDL2)
	else()
		message(STATUS "no TARGET SDL2::SDL2, or SDL2, using variables")
		target_include_directories(Refresh PUBLIC "$<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>")
		target_link_libraries(Refresh PUBLIC ${SDL2_LIBRARIES})
	endif()
endif()
