#!/usr/bin/env python3
import can
import time
import threading
import struct
from datetime import datetime
import logging

# 配置日志只输出到命令行
logging.basicConfig(
    level=logging.INFO,  # 设置为INFO级别以减少输出
    format='%(asctime)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger('CAN_Simulator')

# ===== DBC 信号定义 =====
# 发动机系统
ENGINE_DATA_ID = 100       # 100
GEARBOX_ID = 101           # 101
FUEL_SYSTEM_ID = 102       # 102

# 车身系统
DOOR_STATUS_ID = 200       # 200
WINDOW_STATUS_ID = 201     # 201
LIGHTING_ID = 202          # 202

# 仪表盘系统
VEHICLE_STATUS_ID = 300   # 300
WARNING_LIGHTS_ID = 301   # 301

# 传输层帧类型
SINGLE_FRAME = 0
FIRST_FRAME = 1
CONSECUTIVE_FRAME = 2
FLOW_CONTROL_FRAME = 3

class VehicleSimulator:
    def __init__(self):
        self.bus = self.setup_can_bus()
        self.running = True
        self.last_update = time.time()
        
        # 车辆状态存储
        self.vehicle_state = {
            'rpm': 800,
            'speed': 0,
            'gear': 0,
            'throttle': 0,
            'fuel_level': 80,
            'coolant_temp': 90,
            'gear_mode': 3,  # D模式
            'fuel_consumption': 7.5,
            'check_engine': 0,
            'oil_pressure': 0,
            'battery': 0,
            'driver_door': 0,
            'passenger_door': 0,
            'trunk': 0,
            'hood': 0,
            'driver_window': 0,
            'passenger_window': 0,
            'headlights': 0,
            'brake_lights': 0,
            'indicator_left': 0,
            'indicator_right': 0
        }
        
        # 多帧消息组装缓冲区
        self.rx_assemblers = {}  # {arbitration_id: {total_length, data, next_sequence}}
        self.tx_sequence = 0
    
    def setup_can_bus(self):
        """创建并配置CAN总线接口"""
        try:
            # 尝试使用虚拟接口
            bus = can.interface.Bus(
                interface='pcan',
                channel='PCAN_USBBUS1',
                bitrate=500000,
                receive_own_messages=False  # 避免接收自己发送的消息
            )
            logger.info("使用虚拟CAN接口")
            return bus
        except Exception as ve:
            logger.error(f"无法初始化CAN接口: {ve}")
            return None
    
    def log_raw_message(self, msg, direction):
        """记录原始CAN消息"""
        data_hex = ' '.join(f"{b:02X}" for b in msg.data)
        
        # 解析帧类型
        if len(msg.data) > 0:
            pci = msg.data[0]
            frame_type = (pci >> 4) & 0x0F
            
            if frame_type == SINGLE_FRAME:
                length = pci & 0x0F
                frame_info = f"单帧(SF) 长度={length}"
            elif frame_type == FIRST_FRAME:
                if len(msg.data) > 1:
                    # 正确计算12位长度
                    total_length = ((pci & 0x0F) << 8) | msg.data[1]
                    frame_info = f"首帧(FF) 总长度={total_length}"
                else:
                    frame_info = "首帧(FF) 无效格式"
            elif frame_type == CONSECUTIVE_FRAME:
                sequence = pci & 0x0F
                frame_info = f"连续帧(CF) 序列号={sequence}"
            else:
                frame_info = f"未知帧类型({frame_type})"
        else:
            frame_info = "空帧"
        
        logger.info(f"{direction}报文: ID=0x{msg.arbitration_id:03X}, {frame_info}, 数据=[{data_hex}]")
    
    def parse_isotp_frame(self, data):
        """解析ISO-TP帧格式"""
        if not data:
            return None, None, None
        
        pci_byte = data[0]
        frame_type = (pci_byte >> 4) & 0x0F
        
        if frame_type == SINGLE_FRAME:
            length = pci_byte & 0x0F
            if len(data) < 1 + length:
                return None, None, None
            return frame_type, data[1:1+length], length
        
        elif frame_type == FIRST_FRAME:
            if len(data) < 2:
                return None, None, None
            # 正确计算12位长度
            total_length = ((pci_byte & 0x0F) << 8) | data[1]
            # 计算首帧可携带数据量（最多6字节）
            data_len = min(6, total_length, len(data)-2)
            return frame_type, data[2:2+data_len], total_length
        
        elif frame_type == CONSECUTIVE_FRAME:
            seq_num = pci_byte & 0x0F
            # 返回所有可能数据（最多7字节）
            payload = data[1:1+7] if len(data) > 1 else bytearray()
            return frame_type, payload, seq_num
        
        return None, None, None
    
    def assemble_isotp_message(self, can_id, payload):
        """组装ISO-TP消息"""
        frames = []
        payload_length = len(payload)
        
        if payload_length <= 7:
            # 单帧处理
            frame = bytearray()
            frame.append((SINGLE_FRAME << 4) | payload_length)  # PCI字节: 高4位=0, 低4位=长度
            frame.extend(payload)
            frames.append((can_id, frame))
        else:
            # 首帧
            frame = bytearray()
            frame.append((FIRST_FRAME << 4) | ((payload_length >> 8) & 0x0F))  # PCI1: 高4位=1, 低4位=长度高4位
            frame.append(payload_length & 0xFF)  # PCI2: 长度低8位
            # 首帧最多包含6字节数据
            frame.extend(payload[:6])
            frames.append((can_id, frame))
            
            # 连续帧
            remaining = payload[6:]
            sequence = 0  # 连续帧序列号从0开始（修正）
            
            while remaining:
                frame = bytearray()
                frame.append((CONSECUTIVE_FRAME << 4) | (sequence & 0x0F))  # PCI: 高4位=2, 低4位=序列号
                
                # 取最多7字节
                chunk = remaining[:7]
                frame.extend(chunk)
                frames.append((can_id, frame))
                
                remaining = remaining[7:]
                sequence = (sequence + 1) % 16  # 序列号0-15循环
        
        return frames
    
    def update_state_from_message(self, msg):
        """根据接收到的CAN消息更新车辆状态"""
        # 首先记录原始报文
        self.log_raw_message(msg, "接收")
        
        try:
            # 解析ISO-TP帧
            frame_type, payload, param = self.parse_isotp_frame(msg.data)
            
            if frame_type is None:
                logger.warning(f"无法解析帧: ID=0x{msg.arbitration_id:X}, 数据={msg.data.hex()}")
                return
            
            # 处理完整消息（仅单帧或组装后的多帧）
            if frame_type == SINGLE_FRAME:
                self.process_complete_message(msg.arbitration_id, payload)
            
            elif frame_type == FIRST_FRAME:
                # 初始化组装器
                self.rx_assemblers[msg.arbitration_id] = {
                    'total_length': param,
                    'data': bytearray(payload),
                    'next_sequence': 0  # 期待的第一个连续帧序列号应为0
                }
                logger.info(f"开始组装多帧消息: ID=0x{msg.arbitration_id:X}, 总长度={param}")
            
            elif frame_type == CONSECUTIVE_FRAME:
                # 继续组装多帧消息
                assembler = self.rx_assemblers.get(msg.arbitration_id)
                if assembler:
                    if param == assembler['next_sequence']:
                        # 计算剩余需要的数据长度
                        remaining_length = assembler['total_length'] - len(assembler['data'])
                        # 只取需要的字节数
                        chunk_size = min(remaining_length, len(payload))
                        assembler['data'].extend(payload[:chunk_size])
                        
                        # 更新下一个序列号
                        assembler['next_sequence'] = (param + 1) % 16
                        
                        # 检查是否完成
                        if len(assembler['data']) >= assembler['total_length']:
                            logger.info(f"完成组装多帧消息: ID=0x{msg.arbitration_id:X}, 长度={assembler['total_length']}")
                            self.process_complete_message(
                                msg.arbitration_id, 
                                assembler['data'][:assembler['total_length']]
                            )
                            del self.rx_assemblers[msg.arbitration_id]
                    else:
                        logger.warning(f"序列号不匹配! 预期: {assembler['next_sequence']}, 实际: {param}")
                        # 序列号不匹配，丢弃当前组装器
                        del self.rx_assemblers[msg.arbitration_id]
        
        except Exception as e:
            logger.error(f"处理消息时出错: {e}")
    
    def process_complete_message(self, can_id, payload):
        """处理完整的应用层消息"""
        try:
            logger.info(f"应用层消息: ID=0x{can_id:03X}, 长度={len(payload)}, 数据=[{' '.join(f'{b:02X}' for b in payload)}]")
            
            # 发动机数据 (ID 100)
            if can_id == ENGINE_DATA_ID and len(payload) >= 8:  # 确保有8字节数据
                # RPM: 起始位0，长度16 (大端)
                # 字节0-1: 高位在前 (大端)
                rpm_raw = (payload[0] << 8) | payload[1]
                self.vehicle_state['rpm'] = rpm_raw * 0.125  # 缩放0.125
                
                # SPEED: 起始位16，长度16 (大端)
                # 字节2-3: 高位在前 (大端)
                speed_raw = (payload[2] << 8) | payload[3]
                self.vehicle_state['speed'] = speed_raw * 0.01  # 缩放0.01
                
                # COOLANT_TEMP: 起始位32，长度8
                # 字节4
                coolant_raw = payload[4]
                self.vehicle_state['coolant_temp'] = coolant_raw - 40  # 偏移-40
                
                # THROTTLE_POS: 起始位40，长度8
                # 字节5
                throttle_raw = payload[5]
                self.vehicle_state['throttle'] = throttle_raw * 0.392  # 缩放0.392
                
                logger.info(f"  更新: 速度={self.vehicle_state['speed']:.1f} km/h, "
                            f"RPM={self.vehicle_state['rpm']:.0f}, "
                            f"冷却液={self.vehicle_state['coolant_temp']}°C, "
                            f"节气门={self.vehicle_state['throttle']:.1f}%")
            
            # 变速箱数据 (ID 101)
            elif can_id == GEARBOX_ID and len(payload) >= 1:
                # CURRENT_GEAR: 起始位0，长度3
                gear_data = payload[0]
                self.vehicle_state['gear'] = gear_data & 0x07  # 取低3位
                
                # GEAR_MODE: 起始位3，长度2
                self.vehicle_state['gear_mode'] = (gear_data >> 3) & 0x03  # 取第3-4位
                
                logger.info(f"  更新: 挡位={self.vehicle_state['gear']}, 模式={self.vehicle_state['gear_mode']}")
            
            # 燃油系统数据 (ID 102)
            elif can_id == FUEL_SYSTEM_ID and len(payload) >= 3:
                # FUEL_LEVEL: 起始位0，长度8
                # 字节0
                self.vehicle_state['fuel_level'] = payload[0] * 0.5  # 缩放0.5
                
                # FUEL_CONSUMPTION: 起始位8，长度16 (大端)
                # 字节1-2: 高位在前 (大端)
                fuel_cons_raw = (payload[1] << 8) | payload[2]
                self.vehicle_state['fuel_consumption'] = fuel_cons_raw * 0.01  # 缩放0.01
                
                logger.info(f"  更新: 油量={self.vehicle_state['fuel_level']:.1f}%, "
                            f"油耗={self.vehicle_state['fuel_consumption']:.2f} L/100km")
            
            # 车辆状态数据 (ID 300)
            elif can_id == VEHICLE_STATUS_ID and len(payload) >= 6:
                # 解析速度 (0.01 km/h)
                speed_val = (payload[0] << 8) | payload[1]
                self.vehicle_state['speed'] = speed_val * 0.01
                
                # 解析RPM (0.125 RPM)
                rpm_val = (payload[2] << 8) | payload[3]
                self.vehicle_state['rpm'] = rpm_val * 0.125
                
                # 解析燃油油位 (0.5%)
                self.vehicle_state['fuel_level'] = payload[4] * 0.5
                
                # 解析挡位
                self.vehicle_state['gear'] = payload[5] & 0x07
                logger.info(f"  更新: 速度={self.vehicle_state['speed']:.1f} km/h, RPM={self.vehicle_state['rpm']:.0f}, 油量={self.vehicle_state['fuel_level']:.1f}%, 挡位={self.vehicle_state['gear']}")
            
            # 警告灯数据 (ID 301)
            elif can_id == WARNING_LIGHTS_ID and len(payload) >= 1:
                warnings = payload[0]
                self.vehicle_state['check_engine'] = warnings & 0x01
                self.vehicle_state['oil_pressure'] = (warnings >> 1) & 0x01
                self.vehicle_state['battery'] = (warnings >> 2) & 0x01
                logger.info(f"  更新: 检查引擎={self.vehicle_state['check_engine']}, 油压={self.vehicle_state['oil_pressure']}, 电池={self.vehicle_state['battery']}")
            
            # 车门状态 (ID 200)
            elif can_id == DOOR_STATUS_ID and len(payload) >= 1:
                door_data = payload[0]
                self.vehicle_state['driver_door'] = door_data & 0x01
                self.vehicle_state['passenger_door'] = (door_data >> 1) & 0x01
                self.vehicle_state['trunk'] = (door_data >> 2) & 0x01
                self.vehicle_state['hood'] = (door_data >> 3) & 0x01
                logger.info(f"  更新: 驾驶座={self.vehicle_state['driver_door']}, 乘客座={self.vehicle_state['passenger_door']}, 后备箱={self.vehicle_state['trunk']}, 引擎盖={self.vehicle_state['hood']}")
            
            # 车窗状态 (ID 201)
            elif can_id == WINDOW_STATUS_ID and len(payload) >= 1:
                window_data = payload[0]
                self.vehicle_state['driver_window'] = window_data & 0x01
                self.vehicle_state['passenger_window'] = (window_data >> 1) & 0x01
                logger.info(f"  更新: 驾驶座={self.vehicle_state['driver_window']}, 乘客座={self.vehicle_state['passenger_window']}")
            
            # 灯光状态 (ID 202)
            elif can_id == LIGHTING_ID and len(payload) >= 1:
                light_data = payload[0]
                self.vehicle_state['headlights'] = light_data & 0x01
                self.vehicle_state['brake_lights'] = (light_data >> 1) & 0x01
                self.vehicle_state['indicator_left'] = (light_data >> 2) & 0x01
                self.vehicle_state['indicator_right'] = (light_data >> 3) & 0x01
                logger.info(f"  更新: 大灯={self.vehicle_state['headlights']}, 刹车灯={self.vehicle_state['brake_lights']}, 左转灯={self.vehicle_state['indicator_left']}, 右转灯={self.vehicle_state['indicator_right']}")
            else:
                logger.info(f"收到未处理的应用层消息: ID=0x{can_id:03X}")
        
        except Exception as e:
            logger.error(f"处理完整消息时出错: {e}")
    
    def generate_engine_data(self):
        """生成发动机数据报文"""
        rpm_val = int(self.vehicle_state['rpm'] / 0.125)  # 转换为0.125精度
        speed_val = int(self.vehicle_state['speed'] * 100)  # 转换为0.01精度
        
        payload = bytearray([
            0x03,                      # 固定字节
            (speed_val >> 8) & 0xFF,   # 速度高字节
            speed_val & 0xFF,          # 速度低字节
            0x00,                      # 固定字节
            (rpm_val >> 8) & 0xFF,     # RPM高字节
            rpm_val & 0xFF             # RPM低字节
        ])
        
        return ENGINE_DATA_ID, payload
    
    def generate_gearbox_data(self):
        """生成变速箱数据报文"""
        payload = bytearray([
            self.vehicle_state['gear'] | (self.vehicle_state['gear_mode'] << 3)
        ])
        
        return GEARBOX_ID, payload
    
    def generate_fuel_data(self):
        """生成燃油系统数据报文"""
        fuel_consumption_int = int(self.vehicle_state['fuel_consumption'] * 100)
        payload = bytearray([
            int(self.vehicle_state['fuel_level'] * 2),  # 燃油水平 (0.5精度)
            (fuel_consumption_int >> 8) & 0xFF,         # 油耗高字节
            fuel_consumption_int & 0xFF,                # 油耗低字节
        ])
        
        return FUEL_SYSTEM_ID, payload
    
    def generate_dashboard_data(self):
        """生成仪表盘数据报文"""
        speed_val = int(self.vehicle_state['speed'] * 100)  # 转换为0.01精度
        rpm_val = int(self.vehicle_state['rpm'] / 0.125)    # 转换为0.125精度
        
        # 构建VEHICLE_STATUS消息 (6字节)
        payload = bytearray([
            (speed_val >> 8) & 0xFF,  # 速度高字节
            speed_val & 0xFF,         # 速度低字节
            (rpm_val >> 8) & 0xFF,    # RPM高字节
            rpm_val & 0xFF,           # RPM低字节
            int(self.vehicle_state['fuel_level'] * 2),  # 燃油油位 (0.5精度)
            self.vehicle_state['gear']  # 挡位
        ])
        
        # 构建警告灯消息
        warnings = bytearray([
            self.vehicle_state['check_engine'] | 
            (self.vehicle_state['oil_pressure'] << 1) | 
            (self.vehicle_state['battery'] << 2)
        ])
        
        return [
            (VEHICLE_STATUS_ID, payload),
            (WARNING_LIGHTS_ID, warnings)
        ]
    
    def generate_body_data(self):
        """生成车身数据报文"""
        door_payload = bytearray([
            self.vehicle_state['driver_door'] | 
            (self.vehicle_state['passenger_door'] << 1) | 
            (self.vehicle_state['trunk'] << 2) | 
            (self.vehicle_state['hood'] << 3)
        ])
        
        window_payload = bytearray([
            self.vehicle_state['driver_window'] | 
            (self.vehicle_state['passenger_window'] << 1)
        ])
        
        light_payload = bytearray([
            self.vehicle_state['headlights'] | 
            (self.vehicle_state['brake_lights'] << 1) | 
            (self.vehicle_state['indicator_left'] << 2) | 
            (self.vehicle_state['indicator_right'] << 3)
        ])
        
        return [
            (DOOR_STATUS_ID, door_payload),
            (WINDOW_STATUS_ID, window_payload),
            (LIGHTING_ID, light_payload)
        ]
    
    def send_messages(self, messages):
        """发送一组CAN消息，使用ISO-TP帧格式"""
        if self.bus is None:
            return
            
        for can_id, payload in messages:
            # 组装ISO-TP帧
            frames = self.assemble_isotp_message(can_id, payload)
            
            for frame_id, frame_data in frames:
                try:
                    # 确保帧数据不超过8字节
                    if len(frame_data) > 8:
                        logger.warning(f"帧数据过长({len(frame_data)}字节)，将被截断")
                        frame_data = frame_data[:8]
                    
                    # 创建CAN消息
                    msg = can.Message(
                        arbitration_id=frame_id,
                        data=frame_data,
                        is_extended_id=False,
                        dlc=len(frame_data)
                    )
                    
                    # 发送消息
                    self.bus.send(msg, timeout=0.1)
                    
                    # 记录发送的消息
                    # self.log_raw_message(msg, "发送")
                except can.CanError as e:
                    logger.error(f"发送消息错误: {e}")
                except Exception as e:
                    logger.error(f"发送消息时发生意外错误: {e}")
    
    def receive_messages(self):
        """接收并处理CAN消息"""
        if self.bus is None:
            logger.error("无法接收消息 - 总线未初始化")
            return
            
        logger.info("开始接收CAN消息...")
        while self.running:
            try:
                msg = self.bus.recv(timeout=0.5)  # 增加超时时间
                if msg is not None and not msg.is_error_frame:
                    # 更新车辆状态
                    self.update_state_from_message(msg)
            except can.CanError as e:
                logger.error(f"接收错误: {e}")
            except Exception as e:
                logger.error(f"处理消息时发生意外错误: {e}")
    
    def shutdown(self):
        self.running = False
        if self.bus is not None:
            try:
                self.bus.shutdown()
                logger.info("CAN总线已关闭")
            except Exception as e:
                logger.error(f"关闭总线时出错: {e}")

# ===== 主程序 =====
def main():
    logger.info("启动车辆模拟器...")
    
    # 创建车辆模拟器
    simulator = VehicleSimulator()
    
    # 启动接收线程
    receiver_thread = threading.Thread(
        target=simulator.receive_messages
    )
    receiver_thread.daemon = True
    receiver_thread.start()
    
    try:
        logger.info("车辆模拟器已启动. 按Ctrl+C退出...")
        
        # 显示实时状态
        print("时间      | 速度 (km/h) | RPM   | 挡位 | 油量 (%) | 警告灯")
        print("----------------------------------------------------------------")
        
        # 初始发送一次
        simulator.send_messages([
            (ENGINE_DATA_ID, simulator.generate_engine_data()[1]),
            (GEARBOX_ID, simulator.generate_gearbox_data()[1]),
            (FUEL_SYSTEM_ID, simulator.generate_fuel_data()[1])
        ])
        simulator.send_messages(simulator.generate_dashboard_data())
        simulator.send_messages(simulator.generate_body_data())
        
        while simulator.running:
            # 生成并发送车辆状态报文
            engine_id, engine_payload = simulator.generate_engine_data()
            gear_id, gear_payload = simulator.generate_gearbox_data()
            fuel_id, fuel_payload = simulator.generate_fuel_data()
            dashboard_msgs = simulator.generate_dashboard_data()
            body_msgs = simulator.generate_body_data()
            
            # 发送所有消息
            simulator.send_messages([
                (engine_id, engine_payload),
                (gear_id, gear_payload),
                (fuel_id, fuel_payload)
            ])
            simulator.send_messages(dashboard_msgs)
            simulator.send_messages(body_msgs)
            
            # 显示当前状态
            state = simulator.vehicle_state
            current_time = datetime.now().strftime("%H:%M:%S")
            
            # 格式化警告灯状态
            warnings = []
            if state['check_engine']: warnings.append("检查引擎")
            if state['oil_pressure']: warnings.append("油压")
            if state['battery']: warnings.append("电池")
            warning_str = ", ".join(warnings) if warnings else "无"
            
            print(
                f"{current_time} | {state['speed']:9.1f} | {state['rpm']:6.0f} | "
                f"{state['gear']:4} | {state['fuel_level']:8.1f} | {warning_str}"
            )
            
            # 控制更新频率
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        logger.info("\n正在停止模拟器...")
        simulator.running = False
    
    finally:
        simulator.shutdown()
        if receiver_thread.is_alive():
            receiver_thread.join(timeout=1.0)
        logger.info("模拟器已关闭")

if __name__ == "__main__":
    main()