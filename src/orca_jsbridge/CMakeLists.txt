# orca_jsbridge: JSON protocol, API registry, permission policy and event push
# over orca_webview. Depends only on orca_webview and nlohmann_json.

add_library(orca_jsbridge SHARED)
add_library(orca::jsbridge ALIAS orca_jsbridge)

# Embed sdk/orca_sdk.js into a generated TU (declared in src/sdk_script.hpp).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedJs.cmake)

set(ORCA_SDK_JS_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/orca_sdk_js.cpp)
orca_embed_js(sdk/orca_sdk.js ${ORCA_SDK_JS_CPP})

target_sources(orca_jsbridge PRIVATE
    src/protocol.cpp
    src/channel.cpp
    src/bridge.cpp
    ${ORCA_SDK_JS_CPP}
)

target_link_libraries(orca_jsbridge PRIVATE orca::webview nlohmann_json)

target_include_directories(orca_jsbridge
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(orca_jsbridge PUBLIC cxx_std_17)
target_compile_definitions(orca_jsbridge PRIVATE ORCA_JSBRIDGE_EXPORTS)

# Keep the DLL next to the application binary for dev runs and ctest.
set_target_properties(orca_jsbridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    ARCHIVE_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    FOLDER "OrcaWebView")

if (WIN32)
    install(TARGETS orca_jsbridge RUNTIME DESTINATION ".")
endif ()
