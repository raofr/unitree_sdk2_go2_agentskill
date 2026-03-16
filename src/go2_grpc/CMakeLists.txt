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
target_link_libraries(go2_sport_grpc_server PRIVATE
    go2_sport_proto
    unitree_sdk2
    Threads::Threads)
