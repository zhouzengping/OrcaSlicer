# orca_plugin_host: plugin manifest, session lifecycle and permission storage
# over orca_webview + orca_jsbridge. Groundwork for the plugin marketplace.

add_library(orca_plugin_host SHARED)
add_library(orca::plugin ALIAS orca_plugin_host)

target_sources(orca_plugin_host PRIVATE
    src/manifest.cpp
    src/permission_store.cpp
    src/plugin_host.cpp
    src/semver.cpp
)

target_link_libraries(orca_plugin_host PRIVATE orca::webview orca::jsbridge nlohmann_json)

target_include_directories(orca_plugin_host
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(orca_plugin_host PUBLIC cxx_std_17)
target_compile_definitions(orca_plugin_host PRIVATE ORCA_PLUGIN_HOST_EXPORTS)

# Keep the DLL next to the application binary for dev runs and ctest.
set_target_properties(orca_plugin_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    ARCHIVE_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    FOLDER "OrcaWebView")

if (WIN32)
    install(TARGETS orca_plugin_host RUNTIME DESTINATION ".")
endif ()
