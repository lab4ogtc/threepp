
add_example(NAME "simple_skinning" LINK_ASSIMP)
add_example(NAME "assimp_bones" LINK_ASSIMP)

if (APPLE)
    add_example(NAME "simple_skinning_metal" LINK_ASSIMP)
endif ()
