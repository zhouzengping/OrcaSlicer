# Embeds a JavaScript source file into a C++ translation unit as a raw string
# literal. Script-mode entry:
#   cmake -DJS_INPUT=<path> -DCPP_OUTPUT=<path> -P EmbedJsScript.cmake

if (NOT DEFINED JS_INPUT OR NOT DEFINED CPP_OUTPUT)
    message(FATAL_ERROR "EmbedJsScript.cmake requires -DJS_INPUT=<js> -DCPP_OUTPUT=<cpp>")
endif ()

if (NOT EXISTS "${JS_INPUT}")
    message(FATAL_ERROR "EmbedJsScript: input not found: ${JS_INPUT}")
endif ()

file(READ "${JS_INPUT}" content)

# The payload becomes the body of R"__orca_js__( ... )__orca_js__"; refuse any
# occurrence of the closing delimiter inside it.
string(FIND "${content}" ")__orca_js__(" pos)
if (NOT pos EQUAL -1)
    message(FATAL_ERROR "EmbedJsScript: raw string delimiter )__orca_js__( occurs in ${JS_INPUT}")
endif ()

# Keep the closing delimiter on its own line whatever the input ends with.
set(literal "__orca_js__(\n${content}\n)__orca_js__")

get_filename_component(out_dir "${CPP_OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${out_dir}")

file(WRITE "${CPP_OUTPUT}"
"// GENERATED from ${JS_INPUT} -- DO NOT EDIT.
#include \"sdk_script.hpp\"

namespace orca { namespace jb { namespace detail {

const char kSdkInitScript[] = R\"${literal}\";

} } }
")
