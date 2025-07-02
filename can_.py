import can
import time
import random
import binascii

# ========== DBC 信号定义 ==========
# 车身控制信号 (修正 ID)
DOOR_STATUS_ID = 0xC8    # 200
WINDOW_STATUS_ID = 0xC9  # 201
LIGHTING_ID = 0xCA       # 202

# 动力系统信号 (修正 ID)
ENGINE_DATA_ID = 0x64    # 100
GEARBOX_ID = 0x65        # 101
FUEL_SYSTEM_ID = 0x66    # 102

# ========== 配置 PCAN-USB 设备 ==========
def setup_can_bus():
    bus = can.Bus(
        interface='pcan',           # PCAN 设备接口
        channel='PCAN_USBBUS1',     # 默认通道名 (根据设备修改)
        bitrate=500000,             # CAN 波特率 (单位: bps)
        receive_own_messages=True   # 是否接收自己发送的报文
    )
    print(f"PCAN-USB 设备已连接: {bus.channel_info}")
    return bus

# ========== 生成车身控制信号 ==========
def generate_body_control_signals():
    # 车门状态 (0=关闭, 1=打开)
    driver_door = random.choice([0, 1])
    passenger_door = random.choice([0, 1])
    trunk = random.choice([0, 1])
    hood = random.choice([0, 1])
    
    # 车窗位置 (0-100%)
    driver_window = random.randint(0, 100)
    passenger_window = random.randint(0, 100)
    
    # 灯光状态 (0=关闭, 1=打开)
    headlights = random.choice([0, 1])
    brake_lights = random.choice([0, 1])
    indicator_left = random.choice([0, 1])
    indicator_right = random.choice([0, 1])
    
    # 打包车门状态消息 (根据 DBC 定义)
    door_status_data = [
        driver_door | (passenger_door << 1) | (trunk << 2) | (hood << 3),
        0, 0, 0, 0, 0, 0, 0
    ]
    
    # 打包车窗状态消息 (根据 DBC 定义)
    window_status_data = [
        driver_window,
        passenger_window,
        0, 0, 0, 0, 0, 0
    ]
    
    # 打包灯光状态消息 (根据 DBC 定义)
    lighting_data = [
        headlights | (brake_lights << 1) | (indicator_left << 2) | (indicator_right << 3),
        0, 0, 0, 0, 0, 0, 0
    ]
    
    return [
        (DOOR_STATUS_ID, door_status_data),
        (WINDOW_STATUS_ID, window_status_data),
        (LIGHTING_ID, lighting_data)
    ]

# ========== 生成动力系统信号 ==========
def generate_powertrain_signals():
    # 引擎数据
    rpm = random.randint(800, 7000)  # RPM
    speed = random.randint(0, 200)   # km/h
    coolant_temp = random.randint(70, 110)  # °C
    throttle_pos = random.randint(0, 100)   # %
    
    # 变速箱状态
    current_gear = random.randint(0, 7)  # 0-7档
    gear_mode = random.randint(0, 3)     # 0=P, 1=R, 2=N, 3=D
    
    # 燃油系统
    fuel_level = random.randint(0, 100)  # %
    fuel_consumption = random.uniform(5.0, 15.0)  # L/100km
    
    # 打包引擎数据消息
    engine_data = [
        rpm >> 8, rpm & 0xFF,             # RPM (16位)
        speed >> 8, speed & 0xFF,         # 速度 (16位)
        coolant_temp,                     # 冷却液温度
        throttle_pos,                     # 油门位置
        0, 0                              # 填充字节
    ]
    
    # 打包变速箱消息
    gearbox_data = [
        current_gear | (gear_mode << 3),  # 当前档位 + 模式
        0, 0, 0, 0, 0, 0, 0               # 填充字节
    ]
    
    # 打包燃油系统消息
    # 将浮点数转换为固定点表示 (0.01精度)
    fuel_consumption_int = int(fuel_consumption * 100)
    fuel_system_data = [
        fuel_level,                       # 燃油水平
        fuel_consumption_int >> 8,        # 油耗高字节
        fuel_consumption_int & 0xFF,      # 油耗低字节
        0, 0, 0, 0, 0                     # 填充字节
    ]
    
    return [
        (ENGINE_DATA_ID, engine_data),
        (GEARBOX_ID, gearbox_data),
        (FUEL_SYSTEM_ID, fuel_system_data)
    ]

# ========== CAN 报文发送函数 (直接发送) ==========
def send_signals(bus, signals):
    try:
        for can_id, data in signals:
            # 确保数据长度是8字节
            padded_data = data + [0] * (8 - len(data))
            
            msg = can.Message(
                arbitration_id=can_id,
                data=padded_data[:8],
                is_extended_id=False
            )
            bus.send(msg)
            print(f"发送: ID=0x{can_id:03X}, 数据={bytes(padded_data[:8]).hex(' ').upper()}")
    
    except can.CanError as e:
        print(f"发送错误: {e}")

# ========== CAN 报文接收函数 ==========
def receive_can_frames(bus, duration=10):
    print(f"\n===== 开始接收报文 (持续 {duration} 秒) =====")
    start_time = time.time()
    
    try:
        while time.time() - start_time < duration:
            msg = bus.recv(timeout=0.5)  # 0.5秒超时
            if msg is not None:
                # 忽略扩展帧和FD帧
                if msg.is_extended_id or msg.is_fd:
                    continue
                
                # 基础帧信息
                timestamp = f"{msg.timestamp:.3f}s" if msg.timestamp else "N/A"
                base_info = (
                    f"[{timestamp}] ID=0x{msg.arbitration_id:03X} "
                    f"| 数据: {msg.data.hex(' ').upper() if msg.data else '无'} "
                    f"| 长度: {msg.dlc}字节"
                )
                
                # 解析特定ID的消息
                if msg.arbitration_id in [DOOR_STATUS_ID, WINDOW_STATUS_ID, LIGHTING_ID]:
                    print(f"{base_info} | [车身控制信号]")
                elif msg.arbitration_id in [ENGINE_DATA_ID, GEARBOX_ID, FUEL_SYSTEM_ID]:
                    print(f"{base_info} | [动力系统信号]")
                else:
                    print(base_info)
    
    except KeyboardInterrupt:
        pass
    print("接收结束")

# ========== 主测试函数 ==========
def test_vehicle_system(bus, test_duration=30):
    print("\n===== 开始车辆系统测试 =====")
    print("1. 测试车身控制系统 (body_control)")
    print("2. 测试动力系统 (powertrain)")
    print("3. 测试仪表盘 (dashboard)")
    
    # 测试车身控制系统
    print("\n=== 测试车身控制系统 ===")
    for i in range(5):
        body_signals = generate_body_control_signals()
        send_signals(bus, body_signals)
        time.sleep(1)
    
    # 测试动力系统
    print("\n=== 测试动力系统 ===")
    for i in range(5):
        powertrain_signals = generate_powertrain_signals()
        send_signals(bus, powertrain_signals)
        time.sleep(1)
    
    # 同时测试所有系统
    print("\n=== 综合测试所有系统 ===")
    start_time = time.time()
    while time.time() - start_time < test_duration:
        # 随机生成信号
        if random.random() < 0.6:  # 60% 概率生成车身信号
            send_signals(bus, generate_body_control_signals())
        
        if random.random() < 0.7:  # 70% 概率生成动力信号
            send_signals(bus, generate_powertrain_signals())
        
        time.sleep(0.5)

# ========== 主程序 ==========
if __name__ == "__main__":
    # 初始化 CAN 总线
    can_bus = setup_can_bus()
    
    try:
        # 启动接收线程
        import threading
        receiver_thread = threading.Thread(
            target=receive_can_frames, 
            args=(can_bus, 60)  # 接收60秒
        )
        receiver_thread.daemon = True
        receiver_thread.start()
        
        # 运行主测试
        test_vehicle_system(can_bus, test_duration=30)
        
        # 等待接收线程结束
        receiver_thread.join(timeout=5)
        
    except KeyboardInterrupt:
        print("\n测试被用户中断")
    
    finally:
        # 关闭连接
        can_bus.shutdown()
        print("\n设备已断开连接")
        