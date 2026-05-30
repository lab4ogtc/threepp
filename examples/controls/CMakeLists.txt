
add_example(NAME "drag" WEB)
add_example(NAME "transform" WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/textures/crate.gif@data/textures/crate.gif"
)
add_example(NAME "fly" WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/textures/planets@data/textures/planets"
)

if (APPLE)
    add_example(NAME "drag_metal")
endif ()
