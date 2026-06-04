
add_example(NAME "cubemap")
add_example(NAME "texture3d" LINK_IMGUI)
add_example(NAME "texture2d" WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/textures/crate.gif@data/textures/crate.gif"
        "${PROJECT_SOURCE_DIR}/data/textures/checker.png@data/textures/checker.png"
        "${PROJECT_SOURCE_DIR}/data/textures/brick_bump.jpg@data/textures/brick_bump.jpg"
)
if (APPLE)
    add_example(NAME "texture2d_metal")
    add_example(NAME "cubemap_metal")
    add_example(NAME "data_texture_metal")
    add_example(NAME "depth_texture_metal")
    if (THREEPP_WITH_SLANG)
        add_example(NAME "texture3d_metal" LINK_IMGUI)
    endif ()
    add_example(NAME "imgui_framebuffer_metal" LINK_IMGUI)
endif ()
add_example(NAME "data_texture" WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/textures/checker.png@data/textures/checker.png"
        "${PROJECT_SOURCE_DIR}/data/textures/crate.gif@data/textures/crate.gif"
)
add_example(NAME "imgui_framebuffer" LINK_IMGUI WEB)

add_example(NAME "depth_texture" WEB)
