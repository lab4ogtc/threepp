
add_example(NAME "hemi_light" WEB)
add_example(NAME "point_light" WEB)
add_example(NAME "spot_light" WEB)
add_example(NAME "directional" WEB)

if (APPLE)
    add_example(NAME "hemi_light_metal")
    add_example(NAME "point_light_metal")
    add_example(NAME "spot_light_metal")
    add_example(NAME "directional_metal")
endif ()
