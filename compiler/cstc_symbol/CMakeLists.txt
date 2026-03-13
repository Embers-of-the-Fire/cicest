add_library(cstc_symbol INTERFACE)
add_library(cicest::compiler::symbol ALIAS cstc_symbol)

target_include_directories(cstc_symbol INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_symbol
    INTERFACE
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_symbol_${test_name} ${test_source})
        target_link_libraries(cstc_symbol_${test_name} PRIVATE cstc_symbol cicest_options cicest_warnings)
        add_test(NAME cstc_symbol_${test_name} COMMAND cstc_symbol_${test_name})
    endforeach()
endif()
