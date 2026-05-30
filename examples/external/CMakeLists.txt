
add_library(imgui)
add_library(imgui::imgui ALIAS imgui)
target_sources(imgui PRIVATE
        "imgui/imgui.cpp"
        "imgui/imgui_draw.cpp"
        "imgui/imgui_impl_glfw.cpp"
        "imgui/imgui_impl_opengl3.cpp"
        "imgui/imgui_tables.cpp"
        "imgui/imgui_widgets.cpp"
        "${PROJECT_SOURCE_DIR}/src/threepp/extras/imgui/ImguiContextGL.cpp"
)

if (APPLE)
    target_sources(imgui PRIVATE
            "imgui/imgui_impl_metal.mm"
            "${PROJECT_SOURCE_DIR}/src/threepp/extras/imgui/ImguiContextMetal.mm"
    )
    target_link_libraries(imgui PRIVATE
            "-framework Metal"
            "-framework QuartzCore"
    )
endif ()

target_link_libraries(imgui PRIVATE glfw threepp)
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
