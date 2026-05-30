
add_example(NAME "snake" SOURCES main.cpp)

if (APPLE)
    add_example(NAME "snake_metal" SOURCES main_metal.cpp)
endif ()
