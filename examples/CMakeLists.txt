# Copyright 2026 Denis Tishkov
# SPDX-License-Identifier: Apache-2.0

set(TESSERA_EXAMPLES
        01_basics
        02_system_assembly
        03_runtime_dispatch
        04_dependency_resolution)

foreach(example IN LISTS TESSERA_EXAMPLES)
    add_executable(tessera_example_${example} ${example}.cpp)
    target_link_libraries(tessera_example_${example} PRIVATE tessera::tessera tessera_project_options)
    set_target_properties(tessera_example_${example} PROPERTIES OUTPUT_NAME ${example})

    if(TESSERA_BUILD_TESTS)
        add_test(NAME example_${example} COMMAND tessera_example_${example})
    endif()
endforeach()
