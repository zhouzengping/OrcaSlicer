#pragma once

/*
 * Export macros for orca_plugin_host.
 * In-tree consumers only (same toolchain, same CRT); no ABI stability promise.
 */

#if defined(_WIN32)
    #if defined(ORCA_PLUGIN_HOST_EXPORTS)
        #define ORCA_PLUGIN_API __declspec(dllexport)
    #else
        #define ORCA_PLUGIN_API __declspec(dllimport)
    #endif
#else
    #define ORCA_PLUGIN_API __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
    // STL members inside exported classes; safe for in-tree consumers sharing the CRT.
    #pragma warning(disable : 4251)
#endif
