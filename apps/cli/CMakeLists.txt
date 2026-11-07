# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(icl_template_cli src/main.cpp)
set_target_properties(icl_template_cli PROPERTIES OUTPUT_NAME icl-template)
target_link_libraries(icl_template_cli
    PRIVATE icl_template::core icl::warnings icl::hardening icl::coverage)

install(TARGETS icl_template_cli RUNTIME DESTINATION bin)

if(ICL_TEMPLATE_BUILD_TESTS)
    # Integration tests run the real executable.
    add_test(NAME cli.version COMMAND icl_template_cli --version)
    set_tests_properties(cli.version PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "^icl-template ${PROJECT_VERSION}")
    add_test(NAME cli.greets COMMAND icl_template_cli Ada)
    set_tests_properties(cli.greets PROPERTIES
        LABELS integration
        PASS_REGULAR_EXPRESSION "Hello, Ada, from ")
    add_test(NAME cli.rejects_extra_arguments COMMAND icl_template_cli one two)
    set_tests_properties(cli.rejects_extra_arguments PROPERTIES LABELS integration WILL_FAIL TRUE)
endif()
