# orca_plugin_host unit tests: manifest schema + permission store persistence.
#
# PluginHost itself is NOT covered here: driving it requires a real webview
# with a message loop; it is exercised manually through the WebViewDevDialog.
# Deliberately NOT linked against test_common / TEST_LINK_LIBS: test_common
# force-includes catch_main.hpp, which pulls libslic3r/boost/openssl headers
# these suites do not need.

if (NOT TARGET orca::plugin)
    return()
endif ()

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    manifest_test.cpp
    permission_store_test.cpp
    )

target_link_libraries(${_TEST_NAME}_tests PRIVATE
    Catch2::Catch2WithMain
    orca::webview
    orca::jsbridge
    orca::plugin
    nlohmann_json
    )

# test_common normally provides TEST_DATA_DIR; these suites skip test_common,
# so point it at tests/data directly (same layout as the other suites).
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../data" _ORCA_TEST_DATA_DIR)
target_compile_definitions(${_TEST_NAME}_tests PRIVATE
    "TEST_DATA_DIR=R\"(${_ORCA_TEST_DATA_DIR})\"")

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # The orca DLLs are emitted next to the app exe; the test exe lives in
    # build/tests/<Config>, so copy them over. This MUST be declared before
    # catch_discover_tests: the discovery step executes the test exe at build
    # time and needs the DLLs in place.
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:orca::webview>
            $<TARGET_FILE:orca::jsbridge>
            $<TARGET_FILE:orca::plugin>
            $<TARGET_FILE_DIR:${_TEST_NAME}_tests>)
endif ()

catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: " EXTRA_ARGS ${CATCH_EXTRA_ARGS})
