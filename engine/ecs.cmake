file(GLOB threshold_ecs_src
    include/donut/ecs/*.h
    src/ecs/*.cpp
    src/ecs/*.h
)

add_library(threshold_ecs STATIC EXCLUDE_FROM_ALL ${threshold_ecs_src})
target_include_directories(threshold_ecs PUBLIC include thirdparty/entt/src)
target_link_libraries(threshold_ecs PUBLIC threshold_core jsoncpp_static)

if(NOT WIN32)
    target_link_libraries(threshold_ecs PRIVATE stdc++fs dl pthread)
endif()

set_target_properties(threshold_ecs PROPERTIES FOLDER Donut)
