add_executable(cstc
    main.cpp
)

target_link_libraries(cstc
    PRIVATE
        cicest_options
        cicest_warnings
        cicest_llvm
)
