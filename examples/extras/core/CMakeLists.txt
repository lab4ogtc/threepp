
add_example(NAME "fonts" LINK_IMGUI WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/fonts@data/fonts"
)

if (APPLE)
    add_example(NAME "fonts_metal" LINK_IMGUI)
endif ()
