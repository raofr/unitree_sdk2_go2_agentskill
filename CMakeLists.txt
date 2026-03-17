cmake_minimum_required(VERSION 3.5)
project(unitree_sdk2 VERSION 2.0.0)

## Project Options
option(BUILD_EXAMPLES "Build examples" ON)
option(BUILD_GO2_GRPC "Build GO2 gRPC server" OFF)

## Set compiler to use c++ 17 features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

## Chosse build type
set(default_build_type "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
            STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif ()

## Use GNUInstallDirs to install libraries into correct locations on all platforms.
include(GNUInstallDirs)
include(CTest)

## Put all binary files into /bin and libraries into /lib
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR})

## Check system architecture
message(STATUS "Current system architecture: ${CMAKE_SYSTEM_PROCESSOR}")

## Import thirdparty libraries
add_subdirectory(thirdparty)

## Import Unitree SDK2 library
set(UNITREE_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/${CMAKE_SYSTEM_PROCESSOR})
find_library(UNITREE_SDK_LIB unitree_sdk2 PATHS ${UNITREE_SDK_PATH} NO_DEFAULT_PATH)

if (NOT UNITREE_SDK_LIB)
    message(FATAL_ERROR "Unitree SDK library for the architecture is not found")
else ()
    message(STATUS "Unitree SDK library found at: ${UNITREE_SDK_LIB}")
endif ()

message(STATUS "Importing: ${UNITREE_SDK_LIB}")

find_package(Threads REQUIRED)

if (BUILD_GO2_GRPC)
    set(GO2_GRPC_USING_CONFIG OFF)

    find_package(Protobuf CONFIG QUIET)
    find_package(gRPC CONFIG QUIET)

    if (TARGET protobuf::libprotobuf AND TARGET gRPC::grpc++)
        set(GO2_GRPC_USING_CONFIG ON)
        set(GO2_GRPCPP_LINK_TARGET gRPC::grpc++)
        set(GO2_GRPC_LINK_TARGET gRPC::grpc)
    else ()
        find_package(Protobuf REQUIRED)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
        pkg_check_modules(GRPC_C REQUIRED IMPORTED_TARGET grpc)

        set(GO2_GRPCPP_LINK_TARGET PkgConfig::GRPCPP)
        set(GO2_GRPC_LINK_TARGET PkgConfig::GRPC_C)
    endif ()

    if (TARGET protobuf::libprotobuf)
        set(GO2_PROTOBUF_LINK_TARGET protobuf::libprotobuf)
    elseif (DEFINED Protobuf_LIBRARIES)
        set(GO2_PROTOBUF_LINK_TARGET ${Protobuf_LIBRARIES})
    elseif (DEFINED PROTOBUF_LIBRARIES)
        set(GO2_PROTOBUF_LINK_TARGET ${PROTOBUF_LIBRARIES})
    else ()
        message(FATAL_ERROR "Unable to locate protobuf link libraries")
    endif ()

    if (NOT Protobuf_PROTOC_EXECUTABLE)
        if (DEFINED PROTOBUF_PROTOC_EXECUTABLE)
            set(Protobuf_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
        endif ()
    endif ()

    find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
    if (NOT GRPC_CPP_PLUGIN_EXECUTABLE)
        message(FATAL_ERROR "grpc_cpp_plugin is required when BUILD_GO2_GRPC=ON")
    endif ()

    set(GO2_PROTOBUF_LINK_TARGET "${GO2_PROTOBUF_LINK_TARGET}" CACHE INTERNAL "go2 grpc protobuf link target")
    set(GO2_GRPCPP_LINK_TARGET "${GO2_GRPCPP_LINK_TARGET}" CACHE INTERNAL "go2 grpc++ link target")
    set(GO2_GRPC_LINK_TARGET "${GO2_GRPC_LINK_TARGET}" CACHE INTERNAL "go2 grpc core link target")
endif ()

add_library(unitree_sdk2 STATIC IMPORTED GLOBAL)
set_target_properties(unitree_sdk2 PROPERTIES
        IMPORTED_LOCATION ${UNITREE_SDK_LIB})
target_link_libraries(unitree_sdk2 INTERFACE ddsc ddscxx Threads::Threads)
target_include_directories(unitree_sdk2 INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

if (BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()

if (BUILD_GO2_GRPC)
    add_subdirectory(src/go2_grpc)
endif ()

## Install the library
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(FILES ${UNITREE_SDK_LIB}
    DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES cmake/unitree_sdk2Targets.cmake
    DESTINATION lib/cmake/unitree_sdk2)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    unitree_sdk2ConfigVersion.cmake
    VERSION "${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}.${${PROJECT_NAME}_VERSION_PATCH}"
    COMPATIBILITY ExactVersion)

configure_file(cmake/unitree_sdk2Config.cmake.in unitree_sdk2Config.cmake @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/unitree_sdk2Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/unitree_sdk2ConfigVersion.cmake"
    DESTINATION lib/cmake/unitree_sdk2)
