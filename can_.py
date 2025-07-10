#!/usr/bin/env python3
import can
import time
import threading
import struct
from datetime import datetime
import logging
import sys

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
DOOR_STATUS_2_ID = 302     # 302 - 新增ID

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
        
        # 状态变化检测
        self.last_state = self.vehicle_state.copy()
        
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
            # 回退到socketcan
            try:
                bus = can.interface.Bus(
                    interface='socketcan',
                    channel='can0',
                    bitrate=500000
                )
                logger.info("使用socketcan接口")
                return bus
            except Exception as e:
                logger.error(f"无法初始化socketcan接口: {e}")
                return None
    
    def log_raw_message(self, msg, direction):
        """记录原始CAN消息 - 改为DEBUG级别"""
        if logger.getEffectiveLevel() > logging.DEBUG:
            return
            
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
        
        logger.debug(f"{direction}报文: ID=0x{msg.arbitration_id:03X}, {frame_info}, 数据=[{data_hex}]")
    
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
            total_length = ((pci_byte & 0x0F) << 8) | data[1]
            data_len = min(6, total_length, len(data)-2)
            return frame_type, data[2:2+data_len], total_length
        
        elif frame_type == CONSECUTIVE_FRAME:
            seq_num = pci_byte & 0x0F
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
            frame.append((SINGLE_FRAME << 4) | payload_length)
            frame.extend(payload)
            frames.append((can_id, frame))
        else:
            # 首帧
            frame = bytearray()
            frame.append((FIRST_FRAME << 4) | ((payload_length >> 8) & 0x0F))
            frame.append(payload_length & 0xFF)
            frame.extend(payload[:6])
            frames.append((can_id, frame))
            
            # 连续帧
            remaining = payload[6:]
            sequence = 0
            
            while remaining:
                frame = bytearray()
                frame.append((CONSECUTIVE_FRAME << 4) | (sequence & 0x0F))
                
                chunk = remaining[:7]
                frame.extend(chunk)
                frames.append((can_id, frame))
                
                remaining = remaining[7:]
                sequence = (sequence + 1) % 16
        
        return frames
    
    def update_state_from_message(self, msg):
        """根据接收到的CAN消息更新车辆状态"""
        # 记录原始报文 (DEBUG级别)
        self.log_raw_message(msg, "接收")
        
        try:
            # 解析ISO-TP帧
            frame_type, payload, param = self.parse_isotp_frame(msg.data)
            
            if frame_type is None:
                logger.debug(f"无法解析帧: ID=0x{msg.arbitration_id:X}")
                return
            
            # 处理完整消息
            if frame_type == SINGLE_FRAME:
                self.process_complete_message(msg.arbitration_id, payload)
            
            elif frame_type == FIRST_FRAME:
                self.rx_assemblers[msg.arbitration_id] = {
                    'total_length': param,
                    'data': bytearray(payload),
                    'next_sequence': 0
                }
                logger.debug(f"开始组装多帧消息: ID=0x{msg.arbitration_id:X}, 总长度={param}")
            
            elif frame_type == CONSECUTIVE_FRAME:
                assembler = self.rx_assemblers.get(msg.arbitration_id)
                if assembler:
                    if param == assembler['next_sequence']:
                        remaining_length = assembler['total_length'] - len(assembler['data'])
                        chunk_size = min(remaining_length, len(payload))
                        assembler['data'].extend(payload[:chunk_size])
                        assembler['next_sequence'] = (param + 1) % 16
                        
                        if len(assembler['data']) >= assembler['total_length']:
                            logger.debug(f"完成组装多帧消息: ID=0x{msg.arbitration_id:X}, 长度={assembler['total_length']}")
                            self.process_complete_message(
                                msg.arbitration_id, 
                                assembler['data'][:assembler['total_length']]
                            )
                            del self.rx_assemblers[msg.arbitration_id]
                    else:
                        logger.warning(f"序列号不匹配! 预期: {assembler['next_sequence']}, 实际: {param}")
                        del self.rx_assemblers[msg.arbitration_id]
        
        except Exception as e:
            logger.error(f"处理消息时出错: {e}")
    
    def process_complete_message(self, can_id, payload):
        """处理完整的应用层消息 - 只在状态变化时打印"""
        try:
            logger.debug(f"应用层消息: ID=0x{can_id:03X}, 长度={len(payload)}")
            
            # 保存旧状态用于比较
            old_state = self.vehicle_state.copy()
            changes = []
            
            # 发动机数据 (ID 100)
            if can_id == ENGINE_DATA_ID and len(payload) >= 6:
                rpm_raw = (payload[0] << 8) | payload[1]
                new_rpm = rpm_raw * 0.125
                if abs(new_rpm - self.vehicle_state['rpm']) > 1:
                    self.vehicle_state['rpm'] = new_rpm
                    changes.append(f"RPM={new_rpm:.0f}")
                
                speed_raw = (payload[2] << 8) | payload[3]
                new_speed = speed_raw * 0.01
                if abs(new_speed - self.vehicle_state['speed']) > 0.1:
                    self.vehicle_state['speed'] = new_speed
                    changes.append(f"速度={new_speed:.1f}km/h")
                
                coolant_raw = payload[4]
                new_coolant = coolant_raw - 40
                if abs(new_coolant - self.vehicle_state['coolant_temp']) > 0.5:
                    self.vehicle_state['coolant_temp'] = new_coolant
                    changes.append(f"冷却液={new_coolant}°C")
                
                throttle_raw = payload[5]
                new_throttle = throttle_raw * 0.392
                if abs(new_throttle - self.vehicle_state['throttle']) > 0.5:
                    self.vehicle_state['throttle'] = new_throttle
                    changes.append(f"节气门={new_throttle:.1f}%")
            
            # 变速箱数据 (ID 101)
            elif can_id == GEARBOX_ID and len(payload) >= 1:
                gear_data = payload[0]
                new_gear = gear_data & 0x07
                if new_gear != self.vehicle_state['gear']:
                    self.vehicle_state['gear'] = new_gear
                    changes.append(f"挡位={new_gear}")
                
                new_gear_mode = (gear_data >> 3) & 0x03
                if new_gear_mode != self.vehicle_state['gear_mode']:
                    self.vehicle_state['gear_mode'] = new_gear_mode
                    changes.append(f"模式={new_gear_mode}")
            
            # 燃油系统数据 (ID 102)
            elif can_id == FUEL_SYSTEM_ID and len(payload) >= 3:
                new_fuel_level = payload[0] * 0.5
                if abs(new_fuel_level - self.vehicle_state['fuel_level']) > 0.5:
                    self.vehicle_state['fuel_level'] = new_fuel_level
                    changes.append(f"油量={new_fuel_level:.1f}%")
                
                fuel_cons_raw = (payload[1] << 8) | payload[2]
                new_fuel_cons = fuel_cons_raw * 0.01
                if abs(new_fuel_cons - self.vehicle_state['fuel_consumption']) > 0.1:
                    self.vehicle_state['fuel_consumption'] = new_fuel_cons
                    changes.append(f"油耗={new_fuel_cons:.2f}L/100km")
            
            # 车辆状态数据 (ID 300)
            elif can_id == VEHICLE_STATUS_ID and len(payload) >= 6:
                speed_val = (payload[0] << 8) | payload[1]
                new_speed = speed_val * 0.01
                if abs(new_speed - self.vehicle_state['speed']) > 0.1:
                    self.vehicle_state['speed'] = new_speed
                    changes.append(f"速度={new_speed:.1f}km/h")
                
                rpm_val = (payload[2] << 8) | payload[3]
                new_rpm = rpm_val * 0.125
                if abs(new_rpm - self.vehicle_state['rpm']) > 1:
                    self.vehicle_state['rpm'] = new_rpm
                    changes.append(f"RPM={new_rpm:.0f}")
                
                new_fuel_level = payload[4] * 0.5
                if abs(new_fuel_level - self.vehicle_state['fuel_level']) > 0.5:
                    self.vehicle_state['fuel_level'] = new_fuel_level
                    changes.append(f"油量={new_fuel_level:.1f}%")
                
                new_gear = payload[5] & 0x07
                if new_gear != self.vehicle_state['gear']:
                    self.vehicle_state['gear'] = new_gear
                    changes.append(f"挡位={new_gear}")
            
            # 警告灯数据 (ID 301)
            elif can_id == WARNING_LIGHTS_ID and len(payload) >= 1:
                warnings = payload[0]
                new_check_engine = warnings & 0x01
                if new_check_engine != self.vehicle_state['check_engine']:
                    self.vehicle_state['check_engine'] = new_check_engine
                    changes.append(f"检查引擎={'ON' if new_check_engine else 'OFF'}")
                
                new_oil_pressure = (warnings >> 1) & 0x01
                if new_oil_pressure != self.vehicle_state['oil_pressure']:
                    self.vehicle_state['oil_pressure'] = new_oil_pressure
                    changes.append(f"油压={'ON' if new_oil_pressure else 'OFF'}")
                
                new_battery = (warnings >> 2) & 0x01
                if new_battery != self.vehicle_state['battery']:
                    self.vehicle_state['battery'] = new_battery
                    changes.append(f"电池={'ON' if new_battery else 'OFF'}")
            
            # 车门状态 (ID 200)
            elif can_id == DOOR_STATUS_ID and len(payload) >= 1:
                door_data = payload[0]
                new_driver_door = door_data & 0x01
                if new_driver_door != self.vehicle_state['driver_door']:
                    self.vehicle_state['driver_door'] = new_driver_door
                    changes.append(f"驾驶座门={'OPEN' if new_driver_door else 'CLOSED'}")
                
                new_passenger_door = (door_data >> 1) & 0x01
                if new_passenger_door != self.vehicle_state['passenger_door']:
                    self.vehicle_state['passenger_door'] = new_passenger_door
                    changes.append(f"乘客座门={'OPEN' if new_passenger_door else 'CLOSED'}")
                
                new_trunk = (door_data >> 2) & 0x01
                if new_trunk != self.vehicle_state['trunk']:
                    self.vehicle_state['trunk'] = new_trunk
                    changes.append(f"后备箱={'OPEN' if new_trunk else 'CLOSED'}")
                
                new_hood = (door_data >> 3) & 0x01
                if new_hood != self.vehicle_state['hood']:
                    self.vehicle_state['hood'] = new_hood
                    changes.append(f"引擎盖={'OPEN' if new_hood else 'CLOSED'}")
            
            # 车窗状态 (ID 201)
            elif can_id == WINDOW_STATUS_ID and len(payload) >= 2:
                # 驾驶员车窗 (0-100%)
                new_driver_window = payload[0]
                if abs(new_driver_window - self.vehicle_state['driver_window']) > 1:
                    self.vehicle_state['driver_window'] = new_driver_window
                    changes.append(f"驾驶座车窗={new_driver_window}%")
                
                # 乘客车窗 (0-100%)
                new_passenger_window = payload[1]
                if abs(new_passenger_window - self.vehicle_state['passenger_window']) > 1:
                    self.vehicle_state['passenger_window'] = new_passenger_window
                    changes.append(f"乘客座车窗={new_passenger_window}%")
            
            # 灯光状态 (ID 202)
            elif can_id == LIGHTING_ID and len(payload) >= 1:
                light_data = payload[0]
                new_headlights = light_data & 0x01
                if new_headlights != self.vehicle_state['headlights']:
                    self.vehicle_state['headlights'] = new_headlights
                    changes.append(f"大灯={'ON' if new_headlights else 'OFF'}")
                
                new_brake_lights = (light_data >> 1) & 0x01
                if new_brake_lights != self.vehicle_state['brake_lights']:
                    self.vehicle_state['brake_lights'] = new_brake_lights
                    changes.append(f"刹车灯={'ON' if new_brake_lights else 'OFF'}")
                
                new_indicator_left = (light_data >> 2) & 0x01
                if new_indicator_left != self.vehicle_state['indicator_left']:
                    self.vehicle_state['indicator_left'] = new_indicator_left
                    changes.append(f"左转灯={'ON' if new_indicator_left else 'OFF'}")
                
                new_indicator_right = (light_data >> 3) & 0x01
                if new_indicator_right != self.vehicle_state['indicator_right']:
                    self.vehicle_state['indicator_right'] = new_indicator_right
                    changes.append(f"右转灯={'ON' if new_indicator_right else 'OFF'}")
            
            # 新增ID 302的门状态处理
            elif can_id == DOOR_STATUS_2_ID and len(payload) >= 1:
                door_data = payload[0]
                new_driver_door = door_data & 0x01
                if new_driver_door != self.vehicle_state['driver_door']:
                    self.vehicle_state['driver_door'] = new_driver_door
                    changes.append(f"302-驾驶座门={'OPEN' if new_driver_door else 'CLOSED'}")
                
                new_passenger_door = (door_data >> 1) & 0x01
                if new_passenger_door != self.vehicle_state['passenger_door']:
                    self.vehicle_state['passenger_door'] = new_passenger_door
                    changes.append(f"302-乘客座门={'OPEN' if new_passenger_door else 'CLOSED'}")
                
                new_trunk = (door_data >> 2) & 0x01
                if new_trunk != self.vehicle_state['trunk']:
                    self.vehicle_state['trunk'] = new_trunk
                    changes.append(f"302-后备箱={'OPEN' if new_trunk else 'CLOSED'}")
            
            # 打印变化
            if changes:
                logger.info(f"状态更新 (ID:0x{can_id:03X}): {', '.join(changes)}")
        
        except Exception as e:
            logger.error(f"处理完整消息时出错: {e}")
    
    def generate_engine_data(self):
        """生成发动机数据报文"""
        rpm_val = int(self.vehicle_state['rpm'] / 0.125)
        speed_val = int(self.vehicle_state['speed'] * 100)
        
        payload = bytearray([
            (rpm_val >> 8) & 0xFF,
            rpm_val & 0xFF,
            (speed_val >> 8) & 0xFF,
            speed_val & 0xFF,
            int(self.vehicle_state['coolant_temp'] + 40),
            int(self.vehicle_state['throttle'] / 0.392)
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
            int(self.vehicle_state['fuel_level'] * 2),
            (fuel_consumption_int >> 8) & 0xFF,
            fuel_consumption_int & 0xFF,
        ])
        
        return FUEL_SYSTEM_ID, payload
    
    def generate_dashboard_data(self):
        """生成仪表盘数据报文"""
        speed_val = int(self.vehicle_state['speed'] * 100)
        rpm_val = int(self.vehicle_state['rpm'] / 0.125)
        
        # 构建VEHICLE_STATUS消息
        payload = bytearray([
            (speed_val >> 8) & 0xFF,
            speed_val & 0xFF,
            (rpm_val >> 8) & 0xFF,
            rpm_val & 0xFF,
            int(self.vehicle_state['fuel_level'] * 2),
            self.vehicle_state['gear']
        ])
        
        # 构建警告灯消息
        warnings = bytearray([
            self.vehicle_state['check_engine'] | 
            (self.vehicle_state['oil_pressure'] << 1) | 
            (self.vehicle_state['battery'] << 2)
        ])
        
        # 添加门状态到仪表盘数据
        door_status = bytearray([
            self.vehicle_state['driver_door'] | 
            (self.vehicle_state['passenger_door'] << 1) | 
            (self.vehicle_state['trunk'] << 2) | 
            (self.vehicle_state['hood'] << 3)
        ])
        
        # 返回所有消息
        return [
            (VEHICLE_STATUS_ID, payload),
            (WARNING_LIGHTS_ID, warnings),
            (DOOR_STATUS_ID, door_status)
        ]
    
    def generate_body_data(self):
        """生成车身数据报文"""
        door_payload = bytearray([
            self.vehicle_state['driver_door'] | 
            (self.vehicle_state['passenger_door'] << 1) | 
            (self.vehicle_state['trunk'] << 2) | 
            (self.vehicle_state['hood'] << 3)
        ])
        
        # 车窗状态现在使用百分比值 (0-100)
        window_payload = bytearray([
            int(self.vehicle_state['driver_window']),
            int(self.vehicle_state['passenger_window'])
        ])
        
        light_payload = bytearray([
            self.vehicle_state['headlights'] | 
            (self.vehicle_state['brake_lights'] << 1) | 
            (self.vehicle_state['indicator_left'] << 2) | 
            (self.vehicle_state['indicator_right'] << 3)
        ])
        
        # 新增ID 302的门状态报文 (只包含三个信号)
        door_payload_302 = bytearray([
            self.vehicle_state['driver_door'] | 
            (self.vehicle_state['passenger_door'] << 1) | 
            (self.vehicle_state['trunk'] << 2)
        ])
        
        return [
            (DOOR_STATUS_ID, door_payload),
            (WINDOW_STATUS_ID, window_payload),
            (LIGHTING_ID, light_payload),
            (DOOR_STATUS_2_ID, door_payload_302)  # 新增ID 302报文
        ]
    
    def send_messages(self, messages):
        """发送一组CAN消息，使用ISO-TP帧格式"""
        if self.bus is None:
            return
            
        for can_id, payload in messages:
            frames = self.assemble_isotp_message(can_id, payload)
            
            for frame_id, frame_data in frames:
                try:
                    if len(frame_data) > 8:
                        frame_data = frame_data[:8]
                    
                    msg = can.Message(
                        arbitration_id=frame_id,
                        data=frame_data,
                        is_extended_id=False,
                        dlc=len(frame_data)
                    )
                    
                    self.bus.send(msg, timeout=0.1)
                    self.log_raw_message(msg, "发送")
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
                msg = self.bus.recv(timeout=0.5)
                if msg is not None and not msg.is_error_frame:
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
    receiver_thread = threading.Thread(target=simulator.receive_messages)
    receiver_thread.daemon = True
    receiver_thread.start()
    
    try:
        logger.info("车辆模拟器已启动. 按Ctrl+C退出...")
        
        # 显示实时状态
        print("时间      | 速度 (km/h) | RPM   | 挡位 | 油量 (%) | 车门状态")
        print("----------------------------------------------------------------")
        
        # 初始发送
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
            
            # 车门状态
            door_status = []
            if state['driver_door']: door_status.append("驾驶门开")
            if state['passenger_door']: door_status.append("乘客门开")
            if state['trunk']: door_status.append("后备箱开")
            if state['hood']: door_status.append("引擎盖开")
            door_str = ", ".join(door_status) if door_status else "全关"
            
            print(
                f"{current_time} | {state['speed']:9.1f} | {state['rpm']:6.0f} | "
                f"{state['gear']:4} | {state['fuel_level']:8.1f} | {door_str}"
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