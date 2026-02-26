add_executable(cstc
    ${CMAKE_CURRENT_SOURCE_DIR}src/main.cpp
)

target_include_directories(cstc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cstc
    PRIVATE
        cicest_options
        cicest_warnings
        cicest_llvm
)
