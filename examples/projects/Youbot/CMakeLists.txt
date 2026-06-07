
add_example(NAME "youbot_kine" LINK_IMGUI)
if (TARGET "youbot_kine")
    target_link_libraries("youbot_kine" PRIVATE kine)
endif ()

if (APPLE)
    add_example(NAME "youbot_kine_metal" SOURCES youbot_kine_metal.cpp LINK_IMGUI)
    if (TARGET "youbot_kine_metal")
        target_link_libraries("youbot_kine_metal" PRIVATE kine)
    endif ()
endif ()
