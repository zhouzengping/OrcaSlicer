# orca_embed_js(<js_input> <cpp_output>)
#
# Adds the build rule that regenerates <cpp_output> from <js_input>.
# <js_input> is interpreted relative to the caller's CMAKE_CURRENT_SOURCE_DIR;
# <cpp_output> should be an absolute path inside the caller's build tree.
# Add <cpp_output> to the target sources afterwards; the custom command marks
# it GENERATED automatically.
function(orca_embed_js js_input cpp_output)
    set(js_abs "${CMAKE_CURRENT_SOURCE_DIR}/${js_input}")

    add_custom_command(
        OUTPUT "${cpp_output}"
        COMMAND ${CMAKE_COMMAND}
                "-DJS_INPUT=${js_abs}"
                "-DCPP_OUTPUT=${cpp_output}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedJsScript.cmake"
        DEPENDS "${js_abs}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedJsScript.cmake"
        COMMENT "Embedding ${js_input} into ${cpp_output}"
        VERBATIM)
endfunction()
