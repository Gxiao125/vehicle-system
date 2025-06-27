cmake_minimum_required(VERSION 3.10)
project(VehicleApplications)

# 设置中间件路径（会被子目录使用）
set(MIDDLEWARE_DIR "/home/xiao/project/vehicle-system/output/middleware" CACHE PATH "Path to middleware")

# 添加子目录
add_subdirectory(body_control)
add_subdirectory(brake_system)
add_subdirectory(dashboard)

# 设置安装前缀（如果未指定）
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/output" CACHE PATH "Install path prefix" FORCE)
endif()

# 添加自定义目标以显示构建信息
add_custom_target(info
    COMMAND ${CMAKE_COMMAND} -E echo "Building Vehicle Applications"
    COMMAND ${CMAKE_COMMAND} -E echo "Middleware path: ${MIDDLEWARE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E echo "Install prefix: ${CMAKE_INSTALL_PREFIX}"
)
