add_library(cicest_rt STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/std/runtime.c
)

# Build with PIC so the archive can be linked into PIE executables.
set_target_properties(cicest_rt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
