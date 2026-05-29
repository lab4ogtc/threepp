
add_example(NAME "helpers" WEB)
add_example(NAME "camera_helper" WEB)
add_example(NAME "depth_sensor" LINK_IMGUI WEB)
add_example(NAME "lidar" LINK_IMGUI WEB)

if (APPLE)
    add_example(NAME "camera_helper_metal")
endif ()
