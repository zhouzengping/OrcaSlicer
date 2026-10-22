#pragma once

/*
 * Export macros for orca_jsbridge.
 * In-tree consumers only (same toolchain, same CRT); no ABI stability promise.
 */

#if defined(_WIN32)
    #if defined(ORCA_JSBRIDGE_EXPORTS)
        #define ORCA_JB_API __declspec(dllexport)
    #else
        #define ORCA_JB_API __declspec(dllimport)
    #endif
#else
    #define ORCA_JB_API __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
    // STL members inside exported classes; safe for in-tree consumers sharing the CRT.
    #pragma warning(disable : 4251)
#endif
