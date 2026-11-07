# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(error_journaling_cli src/main.cpp)
set_target_properties(error_journaling_cli PROPERTIES OUTPUT_NAME error-journaling)
target_link_libraries(error_journaling_cli
    PRIVATE error_journaling::core icl::warnings icl::hardening icl::coverage)

install(TARGETS error_journaling_cli RUNTIME DESTINATION bin)

if(ERROR_JOURNALING_BUILD_TESTS)
    # Integration tests run the real executable.
    add_test(NAME cli.version COMMAND error_journaling_cli --version)
    set_tests_properties(cli.version PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "^error-journaling ${PROJECT_VERSION}")
    add_test(NAME cli.greets COMMAND error_journaling_cli Ada)
    set_tests_properties(cli.greets PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "Hello, Ada, from ")
    add_test(NAME cli.rejects_extra_arguments COMMAND error_journaling_cli one two)
    set_tests_properties(cli.rejects_extra_arguments PROPERTIES LABELS integration WILL_FAIL TRUE)
endif()
