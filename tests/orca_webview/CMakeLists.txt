# orca_webview unit tests.
#
# Pure-logic checks against the frozen public headers only; no wx message loop
# is involved, so no webview is actually created on any platform.
# Deliberately NOT linked against test_common / TEST_LINK_LIBS: test_common
# force-includes catch_main.hpp, which pulls libslic3r/boost/openssl headers
# these wx-free suites do not need.

if (NOT TARGET orca::webview)
    return()
endif ()

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    status_test.cpp
    )

target_link_libraries(${_TEST_NAME}_tests PRIVATE
    Catch2::Catch2WithMain
    orca::webview
    )

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    # The orca DLLs are emitted next to the app exe; the test exe lives in
    # build/tests/<Config>, so copy them over. This MUST be declared before
    # catch_discover_tests: the discovery step executes the test exe at build
    # time and needs the DLLs in place.
    add_custom_command(TARGET ${_TEST_NAME}_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:orca::webview>
            $<TARGET_FILE_DIR:${_TEST_NAME}_tests>)
endif ()

catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: " EXTRA_ARGS ${CATCH_EXTRA_ARGS})
