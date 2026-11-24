file(GLOB donut_ecs_src
    include/donut/ecs/*.h
    src/ecs/*.cpp
    src/ecs/*.h
)

add_library(donut_ecs STATIC EXCLUDE_FROM_ALL ${donut_ecs_src})
target_include_directories(donut_ecs PUBLIC include thirdparty/entt/src)
target_link_libraries(donut_ecs PUBLIC donut_core jsoncpp_static)

if(NOT WIN32)
    target_link_libraries(donut_ecs PRIVATE stdc++fs dl pthread)
endif()

set_target_properties(donut_ecs PROPERTIES FOLDER Donut)
