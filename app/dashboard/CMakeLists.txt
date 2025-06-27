# 设置C++11标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置中间件路径
set(MIDDLEWARE_DIR "/home/xiao/project/vehicle-system/output/middleware")

add_executable(dashboard dashboard.cpp)

target_compile_features(dashboard PRIVATE cxx_std_11)

# 添加头文件路径
target_include_directories(dashboard PRIVATE
    ${MIDDLEWARE_DIR}/include
    ${PROJECT_SOURCE_DIR}/../Middleware/include
    ${CURSES_INCLUDE_DIR}
)

target_link_libraries(dashboard PRIVATE
    ${MIDDLEWARE_DIR}/lib/libVehicleCommunicationAPI.a
    rt
)

# 安装规则
install(TARGETS dashboard
    DESTINATION bin
)

# 复制DBC文件
install(FILES Dashboard.dbc
    DESTINATION ${CMAKE_INSTALL_PREFIX}/dashboard
)