#pragma once

#if defined(PROJECTCORE_STATIC) || !defined(_WIN32)
#  define PROJECTCORE_API
#elif defined(PROJECTCORE_EXPORTS)
#  define PROJECTCORE_API __declspec(dllexport)
#else
#  define PROJECTCORE_API __declspec(dllimport)
#endif

#if defined(_MSC_VER) && !defined(PROJECTCORE_STATIC)
#  pragma warning(disable : 4251)
#  pragma warning(disable : 4275)
#endif
