
add_example(NAME "SpheroControl" SOURCES main.cpp Sphero.cpp WEB)

if (APPLE)
    add_example(NAME "SpheroControl_metal" SOURCES main_metal.cpp Sphero.cpp)
endif ()
