
add_example(NAME "crane3R" SOURCES main.cpp Crane3R.cpp LINK_IMGUI WEB WEB_EMBED
        "${PROJECT_SOURCE_DIR}/data/fonts@data/fonts"
        "${PROJECT_SOURCE_DIR}/data/models/obj/Crane3R@data/models/obj/Crane3R"
)
if (TARGET "crane3R")
    target_link_libraries("crane3R" PRIVATE kine)
endif ()

if (APPLE)
    add_example(NAME "crane3R_metal" SOURCES main_metal.cpp Crane3R.cpp LINK_IMGUI)
    if (TARGET "crane3R_metal")
        target_link_libraries("crane3R_metal" PRIVATE kine)
    endif ()
endif ()
