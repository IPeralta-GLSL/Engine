include(../../donut/compileshaders.cmake)
file(GLOB shaders "*.hlsl")
file(GLOB sources "*.cpp" "*.h")

set(project editor)
set(folder "Examples/Editor")

threshold_compile_shaders_all_platforms(
    TARGET ${project}_shaders
    PROJECT_NAME "Editor"
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
    FOLDER ${folder}
    OUTPUT_BASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}
)

add_executable(${project} WIN32 ${sources})
target_link_libraries(${project} threshold_render threshold_app threshold_engine threshold_ecs imgui)
target_include_directories(${project} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../rt_bindless)
add_dependencies(${project} ${project}_shaders rt_bindless_shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()
