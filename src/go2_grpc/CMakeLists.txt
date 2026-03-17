set(GO2_SPORT_PROTO ${CMAKE_SOURCE_DIR}/proto/go2_sport.proto)

if (NOT GO2_PROTOBUF_LINK_TARGET)
    message(FATAL_ERROR "GO2_PROTOBUF_LINK_TARGET is not set")
endif ()

if (NOT GO2_GRPCPP_LINK_TARGET)
    message(FATAL_ERROR "GO2_GRPCPP_LINK_TARGET is not set")
endif ()

if (NOT GO2_GRPC_LINK_TARGET)
    message(FATAL_ERROR "GO2_GRPC_LINK_TARGET is not set")
endif ()

if (NOT Protobuf_PROTOC_EXECUTABLE)
    if (TARGET protobuf::protoc)
        get_target_property(Protobuf_PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION)
    elseif (DEFINED PROTOBUF_PROTOC_EXECUTABLE)
        set(Protobuf_PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE})
    endif ()
endif ()

if (NOT Protobuf_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "Protobuf protoc executable not found")
endif ()

set(GO2_SPORT_PROTO_GEN_DIR ${CMAKE_BINARY_DIR}/generated/go2_sport)
file(MAKE_DIRECTORY ${GO2_SPORT_PROTO_GEN_DIR})

set(GO2_SPORT_PROTO_SRCS ${GO2_SPORT_PROTO_GEN_DIR}/go2_sport.pb.cc)
set(GO2_SPORT_PROTO_HDRS ${GO2_SPORT_PROTO_GEN_DIR}/go2_sport.pb.h)
set(GO2_SPORT_GRPC_SRCS ${GO2_SPORT_PROTO_GEN_DIR}/go2_sport.grpc.pb.cc)
set(GO2_SPORT_GRPC_HDRS ${GO2_SPORT_PROTO_GEN_DIR}/go2_sport.grpc.pb.h)

add_custom_command(
    OUTPUT ${GO2_SPORT_PROTO_SRCS} ${GO2_SPORT_PROTO_HDRS} ${GO2_SPORT_GRPC_SRCS} ${GO2_SPORT_GRPC_HDRS}
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
    ARGS
      --proto_path=${CMAKE_SOURCE_DIR}/proto
      --cpp_out=${GO2_SPORT_PROTO_GEN_DIR}
      --grpc_out=${GO2_SPORT_PROTO_GEN_DIR}
      --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
      ${GO2_SPORT_PROTO}
    DEPENDS ${GO2_SPORT_PROTO}
    COMMENT "Generating C++ protobuf/grpc sources for go2_sport.proto"
    VERBATIM)

add_library(go2_sport_proto STATIC
    ${GO2_SPORT_PROTO_SRCS}
    ${GO2_SPORT_GRPC_SRCS})

target_include_directories(go2_sport_proto PUBLIC ${GO2_SPORT_PROTO_GEN_DIR})
target_link_libraries(go2_sport_proto PUBLIC ${GO2_PROTOBUF_LINK_TARGET} ${GO2_GRPCPP_LINK_TARGET} ${GO2_GRPC_LINK_TARGET})

add_executable(go2_sport_grpc_server go2_sport_grpc_server.cpp)
target_include_directories(go2_sport_grpc_server PRIVATE ${GO2_SPORT_PROTO_GEN_DIR})

set(TENSORRT_LIB_DIR "$ENV{TENSORRT_LIB_DIR}")
if (NOT TENSORRT_LIB_DIR)
    set(TENSORRT_LIB_DIR "/usr/lib/aarch64-linux-gnu")
endif ()

find_library(TENSORRT_NVINFER nvinfer HINTS ${TENSORRT_LIB_DIR})
find_library(TENSORRT_NVONNXPARSER nvonnxparser HINTS ${TENSORRT_LIB_DIR})
find_library(TENSORRT_CUDART cudart HINTS ${TENSORRT_LIB_DIR} /usr/local/cuda/lib64)

target_link_libraries(go2_sport_grpc_server PRIVATE
    go2_sport_proto
    unitree_sdk2
    Threads::Threads)

find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-app-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0)
    if (GSTREAMER_FOUND)
        target_include_directories(go2_sport_grpc_server PRIVATE ${GSTREAMER_INCLUDE_DIRS})
        target_link_libraries(go2_sport_grpc_server PRIVATE ${GSTREAMER_LIBRARIES})
        target_compile_options(go2_sport_grpc_server PRIVATE ${GSTREAMER_CFLAGS_OTHER})
        target_compile_definitions(go2_sport_grpc_server PRIVATE GST_USE_UNSTABLE_API)
        message(STATUS "GStreamer WebRTC support enabled for go2_sport_grpc_server")
    else ()
        message(WARNING "GStreamer WebRTC dev libs not found (install libgstreamer-plugins-bad1.0-dev); audio WebRTC manager will run in unavailable mode")
    endif ()
endif ()

find_package(CURL QUIET)
if (CURL_FOUND)
    target_include_directories(go2_sport_grpc_server PRIVATE ${CURL_INCLUDE_DIRS})
    target_link_libraries(go2_sport_grpc_server PRIVATE ${CURL_LIBRARIES})
    message(STATUS "libcurl enabled for local peer signaling")
else ()
    message(WARNING "libcurl not found; local_peer signaling mode will be unavailable")
endif ()

find_package(OpenSSL QUIET)
if (OPENSSL_FOUND)
    target_link_libraries(go2_sport_grpc_server PRIVATE OpenSSL::Crypto)
    target_compile_definitions(go2_sport_grpc_server PRIVATE GO2_HAS_OPENSSL_LINK=1)
    message(STATUS "OpenSSL crypto enabled for LocalSTA encrypted signaling fallback")
else ()
    target_compile_definitions(go2_sport_grpc_server PRIVATE GO2_HAS_OPENSSL_LINK=0)
    message(WARNING "OpenSSL not found; LocalSTA encrypted signaling fallback will be unavailable")
endif ()

find_package(OpenCV QUIET COMPONENTS imgcodecs imgproc)
if (OpenCV_FOUND)
    target_link_libraries(go2_sport_grpc_server PRIVATE ${OpenCV_LIBS})
    target_include_directories(go2_sport_grpc_server PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(go2_sport_grpc_server PRIVATE GO2_HAS_OPENCV_LINK=1)
    message(STATUS "OpenCV enabled for YOLO image preprocess/decode")
else ()
    target_compile_definitions(go2_sport_grpc_server PRIVATE GO2_HAS_OPENCV_LINK=0)
    message(WARNING "OpenCV not found; YOLO inference will decode no frames")
endif ()

if (TENSORRT_NVINFER)
    target_link_libraries(go2_sport_grpc_server PRIVATE ${TENSORRT_NVINFER})
else ()
    message(WARNING "TensorRT nvinfer not found; detection RPCs will report unavailable at runtime")
endif ()

if (TENSORRT_NVONNXPARSER)
    target_link_libraries(go2_sport_grpc_server PRIVATE ${TENSORRT_NVONNXPARSER})
endif ()

if (TENSORRT_CUDART)
    target_link_libraries(go2_sport_grpc_server PRIVATE ${TENSORRT_CUDART})
endif ()

if (BUILD_TESTING)
    add_executable(go2_grpc_proto_smoke_test tests/go2_grpc_proto_smoke_test.cpp)
    target_include_directories(go2_grpc_proto_smoke_test PRIVATE ${GO2_SPORT_PROTO_GEN_DIR})
    target_link_libraries(go2_grpc_proto_smoke_test PRIVATE
        go2_sport_proto
        Threads::Threads)
    add_test(NAME go2_grpc_proto_smoke_test COMMAND go2_grpc_proto_smoke_test)

    add_executable(go2_grpc_audio_mock_test tests/go2_grpc_audio_mock_test.cpp)
    target_link_libraries(go2_grpc_audio_mock_test PRIVATE Threads::Threads)
    add_test(NAME go2_grpc_audio_mock_test COMMAND go2_grpc_audio_mock_test)
endif ()
