cmake_minimum_required(VERSION 3.5)
project(CAN_Communication_System LANGUAGES CXX)

# 设置C++标准为11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 启用调试信息
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3")

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找必需的依赖
find_package(Threads REQUIRED)

# 添加包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# 添加共享内存库
add_library(SharedMemory STATIC
    sharedMemory/SharedMemoryPool.cpp
    include/SharedMemoryPool.h
)

# 链接系统库
target_link_libraries(SharedMemory 
    PUBLIC 
        Threads::Threads
        rt
)

# 添加CAN消息解析库
add_library(CanMessageParser STATIC
    masage_parse/CanMessageParser.cpp
    include/CanMessageParser.h
)

# 链接共享内存库
target_link_libraries(CanMessageParser
    PUBLIC
        SharedMemory
)

# 添加CAN控制器库
add_library(CanController STATIC
    can_server/CanController.cpp
    include/CanController.h
)

# 链接线程库
target_link_libraries(CanController
    PUBLIC
        Threads::Threads
)

# 添加传输层库
add_library(TransportLayer STATIC
    can_server/TransportLayer.cpp
    include/TransportLayer.h
)

# 链接CAN控制器和共享内存库
target_link_libraries(TransportLayer
    PUBLIC
        CanController
        SharedMemory
        Threads::Threads
)

# 添加CAN服务库
add_library(CANService STATIC
    can_server/CANService.cpp
    include/CANService.h
)

# 链接传输层和CAN控制器库
target_link_libraries(CANService
    PUBLIC
        TransportLayer
        CanController
        Threads::Threads
)

# 添加VehicleCommunicationAPI库
add_library(VehicleCommunicationAPI STATIC
    api/VehicleCommunicationAPI.cpp
    include/VehicleCommunicationAPI.h
)

# 链接CAN消息解析和共享内存库
target_link_libraries(VehicleCommunicationAPI
    PUBLIC
        CanMessageParser
        SharedMemory
        Threads::Threads
)

# 添加主服务可执行文件
add_executable(can_service
    server_main/can_service_main.cpp
)

# 链接主服务可执行文件
target_link_libraries(can_service
    PRIVATE
        CANService
        TransportLayer
        CanController
        SharedMemory
        Threads::Threads
        rt
)

# 添加可选的测试目标
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    
    # 创建测试目录
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
    
    # 示例：添加共享内存测试
    add_executable(test_shared_memory
        tests/test_shared_memory.cpp
    )
    target_link_libraries(test_shared_memory
        SharedMemory
        Threads::Threads
        rt
    )
    add_test(NAME SharedMemoryTest COMMAND test_shared_memory)
    
    # 添加CAN消息解析测试
    add_executable(test_message_parser
        tests/test_message_parser.cpp
    )
    target_link_libraries(test_message_parser
        CanMessageParser
        SharedMemory
    )
    add_test(NAME MessageParserTest COMMAND test_message_parser)
endif()

# 安装规则
install(TARGETS 
    SharedMemory
    CanMessageParser
    CanController
    TransportLayer
    CANService
    VehicleCommunicationAPI
    can_service
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)

