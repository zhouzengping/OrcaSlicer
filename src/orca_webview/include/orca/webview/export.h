#pragma once

/*
 * Export macros for orca_webview.
 * In-tree consumers only (same toolchain, same CRT); no ABI stability promise.
 */

#if defined(_WIN32)
    #if defined(ORCA_WEBVIEW_EXPORTS)
        #define ORCA_WV_API __declspec(dllexport)
    #else
        #define ORCA_WV_API __declspec(dllimport)
    #endif
#else
    #define ORCA_WV_API __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
    // STL members inside exported classes; safe for in-tree consumers sharing the CRT.
    #pragma warning(disable : 4251)
#endif
