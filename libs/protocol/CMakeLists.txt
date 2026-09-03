add_library(rss_protocol
    src/PacketCodec.cpp
    src/PacketTypes.cpp
    src/ProtocolError.cpp
    src/StructuredPayload.cpp
    src/TextValidation.cpp
)

target_include_directories(rss_protocol
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rss_protocol PUBLIC cxx_std_20)
rss_enable_project_warnings(rss_protocol)
