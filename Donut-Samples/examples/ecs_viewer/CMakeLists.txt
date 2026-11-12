file(GLOB sources "*.cpp" "*.h")

set(project ecs_viewer)
set(folder "Examples/ECS Viewer")

add_executable(${project} WIN32 ${sources})
target_link_libraries(${project} donut_app donut_ecs)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()
