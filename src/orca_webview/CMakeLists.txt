# orca_webview: wx-free cross-platform webview wrapper (WebView2 / WKWebView / WebKitGTK).
# Part of the plugin-market groundwork; depends on neither wxWidgets nor slic3r code.

add_library(orca_webview SHARED)
add_library(orca::webview ALIAS orca_webview)

target_sources(orca_webview PRIVATE
    src/status.cpp
    src/webview.cpp
    src/string_util.cpp
)

if (WIN32)
    target_sources(orca_webview PRIVATE
        src/backend_win.cpp
        src/backend_win_environment.cpp)
    # Headers only; WebView2Loader.dll is loaded at runtime and is already shipped
    # next to the exe by Snapmaker_Orca_copy_dlls (see root CMakeLists.txt).
    target_include_directories(orca_webview SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../deps/WebView2/include)
elseif (APPLE)
    target_sources(orca_webview PRIVATE src/backend_mac.mm)
    # The backend bridge objects are written for ARC regardless of the
    # global objc arc setting.
    set_source_files_properties(src/backend_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(orca_webview PRIVATE "-framework WebKit" "-framework AppKit")
else ()
    target_sources(orca_webview PRIVATE src/backend_gtk.cpp)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WEBKIT2GTK IMPORTED_TARGET webkit2gtk-4.1)
    if (NOT WEBKIT2GTK_FOUND)
        pkg_check_modules(WEBKIT2GTK IMPORTED_TARGET webkit2gtk-4.0)
    endif ()
    if (NOT WEBKIT2GTK_FOUND)
        message(FATAL_ERROR "orca_webview requires webkit2gtk-4.1 (or 4.0)")
    endif ()
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_link_libraries(orca_webview PRIVATE PkgConfig::WEBKIT2GTK PkgConfig::GTK3)
endif ()

target_include_directories(orca_webview
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_features(orca_webview PUBLIC cxx_std_17)
target_compile_definitions(orca_webview PRIVATE ORCA_WEBVIEW_EXPORTS)

# Keep the DLL next to the application binary for dev runs and ctest.
set_target_properties(orca_webview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    ARCHIVE_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:Snapmaker_Orca>"
    FOLDER "OrcaWebView")

# Ship in the installer next to the app binary, mirroring Snapmaker_Orca itself.
# macOS/Linux install placement is deferred until those platforms are brought up.
if (WIN32)
    install(TARGETS orca_webview RUNTIME DESTINATION ".")
endif ()
