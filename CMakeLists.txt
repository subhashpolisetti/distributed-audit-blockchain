cmake_minimum_required(VERSION 3.14)

# Disabling OpenMP since it was causing issue
if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
    string(REPLACE "-fopenmp" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

project(BlockchainConsensus CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++)
pkg_check_modules(PROTOBUF REQUIRED protobuf)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)


include_directories(
    ${GRPC_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    include
)

link_directories(
    ${GRPC_LIBRARY_DIRS}
    ${PROTOBUF_LIBRARY_DIRS}
)

find_program(PROTOC protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/common.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/common.pb.h
           ${CMAKE_CURRENT_BINARY_DIR}/block_chain.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/block_chain.pb.h
           ${CMAKE_CURRENT_BINARY_DIR}/block_chain.grpc.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/block_chain.grpc.pb.h
           ${CMAKE_CURRENT_BINARY_DIR}/file_audit.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/file_audit.pb.h
           ${CMAKE_CURRENT_BINARY_DIR}/file_audit.grpc.pb.cc
           ${CMAKE_CURRENT_BINARY_DIR}/file_audit.grpc.pb.h
    COMMAND ${PROTOC}
    ARGS -I=${CMAKE_CURRENT_SOURCE_DIR}/proto
         --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/common.proto
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/block_chain.proto
         ${CMAKE_CURRENT_SOURCE_DIR}/proto/file_audit.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/common.proto
            ${CMAKE_CURRENT_SOURCE_DIR}/proto/block_chain.proto
            ${CMAKE_CURRENT_SOURCE_DIR}/proto/file_audit.proto
    COMMENT "Generating protobuf and gRPC files"
)

add_executable(blockchain_node
    src/main.cc
    src/node.cc
    src/blockchain_service.cc
    src/file_audit_service.cc
    src/mempool.cc
    src/block_manager.cc
    src/crypto_utils.cc
    src/config.cc
    ${CMAKE_CURRENT_BINARY_DIR}/common.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/block_chain.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/block_chain.grpc.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/file_audit.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/file_audit.grpc.pb.cc
)

target_link_libraries(blockchain_node
    ${GRPC_LIBRARIES}
    ${PROTOBUF_LIBRARIES}
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    
    absl::base
    absl::strings
    absl::time
    absl::synchronization
    absl::log
    absl::cord
    absl::symbolize
    absl::stacktrace
    absl::debugging
    
    absl::log_internal_check_op
    absl::log_internal_conditions
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::log_internal_proto
    absl::log_internal_format
    absl::log_internal_globals
    absl::log_internal_log_sink_set
    absl::log_sink
    absl::log_entry
    absl::log_globals
    absl::log_initialize
    absl::log_severity
    absl::raw_logging_internal
    absl::flags
    absl::flags_commandlineflag
    absl::flags_config
    absl::flags_internal
    absl::flags_marshalling
    absl::flags_parse
    absl::flags_program_name
    absl::flags_reflection
    absl::hash
    absl::city
    absl::low_level_hash
    absl::random_distributions
    absl::random_internal_distribution_test_util
    absl::random_internal_platform
    absl::random_internal_pool_urbg
    absl::random_internal_randen
    absl::random_internal_randen_hwaes
    absl::random_internal_randen_hwaes_impl
    absl::random_internal_randen_slow
    absl::random_internal_seed_material
    absl::random_seed_gen_exception
    absl::random_seed_sequences
    absl::statusor
    absl::status
    absl::str_format
    absl::str_format_internal
    absl::cord_internal
    absl::cordz_functions
    absl::cordz_handle
    absl::cordz_info
    absl::cordz_sample_token
    absl::exponential_biased
    absl::bad_optional_access
    absl::bad_variant_access
)


# Disabling OpenMP for Apple Clang since it was causing issue
if(APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Disabling OpenMP for Apple Clang")
    add_definitions(-DDISABLE_OPENMP)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-command-line-argument")
    
    set(FILTERED_GRPC_CFLAGS "")
    foreach(FLAG ${GRPC_CFLAGS_OTHER})
        if(NOT "${FLAG}" STREQUAL "-fopenmp")
            list(APPEND FILTERED_GRPC_CFLAGS ${FLAG})
        endif()
    endforeach()
    
    set(FILTERED_PROTOBUF_CFLAGS "")
    foreach(FLAG ${PROTOBUF_CFLAGS_OTHER})
        if(NOT "${FLAG}" STREQUAL "-fopenmp")
            list(APPEND FILTERED_PROTOBUF_CFLAGS ${FLAG})
        endif()
    endforeach()
    
    target_compile_options(blockchain_node PRIVATE
        ${FILTERED_GRPC_CFLAGS}
        ${FILTERED_PROTOBUF_CFLAGS}
    )
else()
    target_compile_options(blockchain_node PRIVATE
        ${GRPC_CFLAGS_OTHER}
        ${PROTOBUF_CFLAGS_OTHER}
    )
endif()

add_custom_command(
    TARGET blockchain_node
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/data
    COMMENT "Creating data directory"
)
