
add_example(NAME "raw_shader")
add_example(NAME "seascape_demo")

if (APPLE)
    add_example(NAME "raw_shader_metal")
    if (THREEPP_WITH_SLANG)
        add_example(NAME "seascape_demo_metal")
    endif ()
endif ()
