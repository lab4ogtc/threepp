
add_example(NAME "lut" WEB LINK_IMGUI)
add_example(NAME "raycast")
add_example(NAME "mouse_key_listener")
add_example(NAME "clipping" LINK_IMGUI)
add_example(NAME "morphtargets" LINK_IMGUI)
add_example(NAME "morphtargets_sphere" LINK_ASSIMP)
add_example(NAME "multiple_scenes")

if (APPLE)
    add_example(NAME "clipping_metal" LINK_IMGUI)
    add_example(NAME "raycast_metal")
    add_example(NAME "multiple_scenes_metal")
endif ()
