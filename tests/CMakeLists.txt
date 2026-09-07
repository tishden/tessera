# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0

add_executable(tessera_tests
        test_main.cpp
        type_list.test.cpp
        mosaic.test.cpp
        value_list.test.cpp
        algebra_backends.test.cpp
        graph.test.cpp
        reflect.test.cpp)

target_link_libraries(tessera_tests PRIVATE tessera::tessera tessera_project_options)
target_include_directories(tessera_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME tessera_tests COMMAND tessera_tests)
