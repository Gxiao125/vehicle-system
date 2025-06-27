# 设置C++11标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置中间件路径
set(MIDDLEWARE_DIR "/home/xiao/project/vehicle-system/output/middleware")

# 检查中间件库是否存在
if(NOT EXISTS "${MIDDLEWARE_DIR}/lib/libVehicleCommunicationAPI.a")
    message(FATAL_ERROR "Middleware library not found. Please build Middleware first.")
endif()

# 添加可执行文件
add_executable(body_control body_control.cpp)

# 设置编译特性
target_compile_features(body_control PRIVATE cxx_std_11)

# 添加头文件路径
target_include_directories(body_control PRIVATE
    ${MIDDLEWARE_DIR}/include
    ${PROJECT_SOURCE_DIR}/../Middleware/include
)

# 链接中间件库
target_link_libraries(body_control PRIVATE
    ${MIDDLEWARE_DIR}/lib/libVehicleCommunicationAPI.a
    rt
)

# 安装规则
install(TARGETS body_control
    DESTINATION bin
)

# 复制DBC文件
install(FILES BodyControl.dbc
    DESTINATION ${CMAKE_INSTALL_PREFIX}/body_control
)

# 添加调试信息
message(STATUS "Configuring body_control application")
message(STATUS "Middleware include path: ${MIDDLEWARE_DIR}/include")
message(STATUS "Middleware library: ${MIDDLEWARE_DIR}/lib/libVehicleMiddleware.a")