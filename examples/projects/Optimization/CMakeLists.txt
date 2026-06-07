
add_example(NAME "Optimization" SOURCES optimization.cpp LINK_IMGUI WEB)
if (TARGET "Optimization")
    target_link_libraries("Optimization" PRIVATE optimization)
endif ()

if (APPLE)
    add_example(NAME "Optimization_metal" SOURCES optimization_metal.cpp LINK_IMGUI)
    if (TARGET "Optimization_metal")
        target_link_libraries("Optimization_metal" PRIVATE optimization)
    endif ()
endif ()
