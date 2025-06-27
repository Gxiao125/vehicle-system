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

# 添加可执行文件 (注意这里使用 powertrain 作为目标名)
add_executable(powertrain powertrain.cpp)

# 设置编译特性
target_compile_features(powertrain PRIVATE cxx_std_11)

# 添加头文件路径
target_include_directories(powertrain PRIVATE
    ${MIDDLEWARE_DIR}/include
    ${PROJECT_SOURCE_DIR}/../Middleware/include
)

# 链接中间件库
target_link_libraries(powertrain PRIVATE
    ${MIDDLEWARE_DIR}/lib/libVehicleCommunicationAPI.a
    rt
)

# 安装规则
install(TARGETS powertrain
    DESTINATION bin
)

# 复制DBC文件
install(FILES Powertrain.dbc
    DESTINATION ${CMAKE_INSTALL_PREFIX}/brake_system
)

# 添加调试信息
message(STATUS "Configuring powertrain application")
message(STATUS "Middleware include path: ${MIDDLEWARE_DIR}/include")
message(STATUS "Middleware library: ${MIDDLEWARE_DIR}/lib/libVehicleMiddleware.a")
