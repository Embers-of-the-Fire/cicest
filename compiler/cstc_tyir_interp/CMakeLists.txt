add_library(cstc_tyir_interp STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/interp.cpp
)
add_library(cicest::compiler::tyir_interp ALIAS cstc_tyir_interp)

target_include_directories(cstc_tyir_interp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(cstc_tyir_interp
    PUBLIC
        cstc_tyir
        cstc_ast
        cstc_symbol
        cstc_span
        cicest_options
        cicest_warnings
)

if(CICEST_BUILD_TESTS)
    file(GLOB glob_test_sources ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

    foreach(test_source ${glob_test_sources})
        get_filename_component(test_name ${test_source} NAME_WE)
        add_executable(cstc_tyir_interp_${test_name} ${test_source})
        target_link_libraries(cstc_tyir_interp_${test_name}
            PRIVATE
                cstc_tyir_interp
                cstc_tyir_builder
                cstc_parser
                cicest_options
                cicest_warnings
        )
        add_test(NAME cstc_tyir_interp_${test_name} COMMAND cstc_tyir_interp_${test_name})
    endforeach()
endif()
