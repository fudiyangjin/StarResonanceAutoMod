"""
网络抓包模块
"""

import socket
import struct
import threading
import time
import logging
from typing import Optional, Callable, Dict, Any
from scapy.all import sniff, IP, TCP, UDP, Raw
import zstandard as zstd
import json
from BlueProtobuf_pb2 import SyncContainerData, CharSerialize, ItemPackage, Package, Item, ModNewAttr
from logging_config import get_logger
from module_parser import ModuleParser

logger = get_logger(__name__)


class BinaryReader:
    """二进制数据读取器"""
    
    def __init__(self, buffer: bytes, offset: int = 0):
        self.buffer = buffer
        self.offset = offset
        
    def readUInt64(self) -> int:
        """读取64位无符号整数(大端序)"""
        value = struct.unpack('>Q', self.buffer[self.offset:self.offset + 8])[0]
        self.offset += 8
        return value
        
    def readUInt32(self) -> int:
        """读取32位无符号整数(大端序)"""
        value = struct.unpack('>I', self.buffer[self.offset:self.offset + 4])[0]
        self.offset += 4
        return value
        
    def peekUInt32(self) -> int:
        """查看32位无符号整数(大端序)，不推进偏移量"""
        return struct.unpack('>I', self.buffer[self.offset:self.offset + 4])[0]
        
    def readUInt16(self) -> int:
        """读取16位无符号整数(大端序)"""
        value = struct.unpack('>H', self.buffer[self.offset:self.offset + 2])[0]
        self.offset += 2
        return value
        
    def readBytes(self, length: int) -> bytes:
        """读取指定长度的字节"""
        value = self.buffer[self.offset:self.offset + length]
        self.offset += length
        return value
        
    def remaining(self) -> int:
        """返回剩余字节数"""
        return len(self.buffer) - self.offset
        
    def readRemaining(self) -> bytes:
        """读取剩余的所有字节"""
        value = self.buffer[self.offset:]
        self.offset = len(self.buffer)
        return value


class PacketCapture:
    """网络数据包抓取器"""
    
    def __init__(self, interface: str = None):
        """
        初始化抓包器
        
        Args:
            interface: 网络接口名称, None表示自动选择
        """
        self.interface = interface
        self.is_running = False
        self.callback = None
        self.packet_count = 0
        self.sync_container_count = 0
        
        self.current_server = ''
        self.tcp_cache = {}
        self.tcp_next_seq = -1
        self.tcp_last_time = 0
        self.tcp_lock = threading.Lock()
        self._data = b''

        self.module_parser = ModuleParser()
        
    def start_capture(self, callback: Callable[[Dict[str, Any]], None] = None):
        """
        开始抓包
        
        Args:
            callback: 数据包处理回调函数
        """
        self.callback = callback
        self.is_running = True
        
        logger.info(f"开始抓包，接口: {self.interface or '自动'}")
        
        # 在新线程中运行抓包
        capture_thread = threading.Thread(target=self._capture_loop)
        capture_thread.daemon = True
        capture_thread.start()
        
        # 启动定时清理线程
        cleanup_thread = threading.Thread(target=self._cleanup_loop)
        cleanup_thread.daemon = True
        cleanup_thread.start()
        
    def stop_capture(self):
        """停止抓包"""
        self.is_running = False
        logger.info("停止抓包")
        
    def _capture_loop(self):
        """抓包主循环"""
        try:
            # 使用scapy进行抓包
            sniff(
                iface=self.interface,
                prn=self._process_packet,
                store=0,
                stop_filter=lambda _: not self.is_running
            )
        except Exception as e:
            logger.error(f"抓包过程中发生错误: {e}")
            
    def _process_packet(self, packet):
        """处理单个数据包"""
        if not self.is_running:
            return
            
        self.packet_count += 1
        
        try:
            # 检查是否是TCP包
            if TCP in packet and IP in packet:
                self._process_tcp_packet(packet)
        except Exception as e:
            logger.debug(f"处理数据包时发生错误: {e}")
            
    def _process_tcp_packet(self, packet):
        """处理TCP数据包"""
        # 获取IP和TCP信息
        ip_layer = packet[IP]
        tcp_layer = packet[TCP]
        
        src_addr = ip_layer.src
        dst_addr = ip_layer.dst
        src_port = tcp_layer.sport
        dst_port = tcp_layer.dport
        seq = tcp_layer.seq
        ack = tcp_layer.ack
        
        # 构建服务器标识
        src_server = f"{src_addr}:{src_port} -> {dst_addr}:{dst_port}"
        
        # 获取TCP负载
        if Raw in packet:
            payload = bytes(packet[Raw])
            self._process_tcp_stream(src_server, seq, payload)
            
    def _process_tcp_stream(self, src_server: str, seq: int, payload: bytes):
        """处理TCP流数据"""
        with self.tcp_lock:
            # 服务器识别逻辑
            if self.current_server != src_server:
                if self._identify_game_server(payload):
                    self.current_server = src_server
                    self._clear_tcp_cache()
                    self.tcp_next_seq = seq + len(payload)
                    logger.info(f'识别到游戏服务器: {src_server}')
                else:
                    return  # 不是游戏服务器，跳过
            
            # 如果还没有识别到服务器，跳过
            if not self.current_server:
                return
                
            # TCP流重组逻辑
            if self.tcp_next_seq == -1:
                logger.error('TCP流重组错误: tcp_next_seq 为 -1')
                if len(payload) > 4 and struct.unpack('>I', payload[:4])[0] < 0x0fffff:
                    self.tcp_next_seq = seq
                return
                
            # 缓存数据包
            if (self.tcp_next_seq - seq) <= 0 or self.tcp_next_seq == -1:
                self.tcp_cache[seq] = payload
                
            # 按顺序处理数据包
            while self.tcp_next_seq in self.tcp_cache:
                seq = self.tcp_next_seq
                cached_data = self.tcp_cache[seq]
                self._data = self._data + cached_data if self._data else cached_data
                self.tcp_next_seq = (seq + len(cached_data)) & 0xffffffff
                del self.tcp_cache[seq]
                self.tcp_last_time = time.time()
                
            # 处理完整的数据包
            self._process_complete_packets()
            
    def _identify_game_server(self, payload: bytes) -> bool:
        """识别游戏服务器"""
        if len(payload) < 10:
            return False
            
        try:
            if payload[4] == 0:
                data = payload[10:]
                if data:
                    # 检查游戏服务器签名
                    signature = b'\x00\x63\x33\x53\x42\x00'
                    if len(data) >= 11 and data[5:11] == signature:
                        return True
                        
            if len(payload) == 0x62:
                # 检查登录返回包特征
                signature = b'\x00\x00\x00\x62\x00\x03\x00\x00\x00\x01'
                if payload[:10] == signature and payload[14:20] == b'\x00\x00\x00\x00\x0a\x4e':
                    return True
                    
        except Exception as e:
            logger.debug(f"服务器识别失败: {e}")
            
        return False
        
    def _clear_tcp_cache(self):
        """清理TCP缓存"""
        self._data = b''
        self.tcp_next_seq = -1
        self.tcp_last_time = 0
        self.tcp_cache.clear()
        
    def _process_complete_packets(self):
        """处理完整的数据包"""
        while len(self._data) > 4:
            try:
                packet_size = struct.unpack('>I', self._data[:4])[0]
                
                if len(self._data) < packet_size:
                    break
                    
                if packet_size > 0x0fffff:
                    logger.error(f"无效的数据包长度: {packet_size}")
                    break
                    
                # 提取完整数据包
                packet = self._data[:packet_size]
                self._data = self._data[packet_size:]
                
                # 分析数据包负载
                self._analyze_payload(packet, "TCP")
                
            except Exception as e:
                logger.debug(f"处理完整数据包失败: {e}")
                break
            
    def _analyze_payload(self, payload: bytes, protocol: str):
        """分析数据包负载"""
        if len(payload) < 4:
            return
            
        try:
            # 尝试解析为SyncContainerData
            parsed_data = self._parse_sync_container_data(payload)
            if parsed_data:
                self.sync_container_count += 1
                logger.debug(f"发现SyncContainerData数据包 #{self.sync_container_count}")
                
                if self.callback:
                    self.callback(parsed_data)
                    
        except Exception as e:
            logger.debug(f"解析数据包失败: {e}")
            
    def _parse_sync_container_data(self, payload: bytes) -> Optional[Dict[str, Any]]:
        """
        解析SyncContainerData数据包
        
        Args:
            payload: 原始数据包负载
            
        Returns:
            解析后的数据, 如果不是SyncContainerData则返回None
        """
        try:
            # 使用BinaryReader进行流式读取
            packets_reader = BinaryReader(payload)
            
            # 处理多个数据包
            while packets_reader.remaining() > 0:
                packet_size = packets_reader.peekUInt32()
                if packet_size < 6:
                    logger.debug("收到无效数据包")
                    return None
                    
                # 读取完整数据包
                packet_data = packets_reader.readBytes(packet_size)
                packet_reader = BinaryReader(packet_data)
                
                # 读取包长度和包类型
                packet_size = packet_reader.readUInt32()
                packet_type = packet_reader.readUInt16()
                
                # 解析包类型
                is_zstd_compressed = (packet_type & 0x8000) != 0
                msg_type_id = packet_type & 0x7fff
                
                # 根据消息类型处理
                if msg_type_id == 2:  # Notify
                    result = self._process_notify_msg(packet_reader, is_zstd_compressed)
                    if result:
                        return result
                elif msg_type_id == 6:  # FrameDown
                    result = self._process_frame_down_msg(packet_reader, is_zstd_compressed)
                    if result:
                        return result
                        
        except Exception as e:
            logger.debug(f"解析SyncContainerData失败: {e}")
            
        return None
        
    def _process_notify_msg(self, reader: BinaryReader, is_zstd_compressed: bool) -> Optional[Dict[str, Any]]:
        """处理Notify消息, 使用流式读取"""
        try:
            # 读取serviceUuid, stubId, methodId
            service_uuid = reader.readUInt64()
            stub_id = reader.readUInt32()
            method_id = reader.readUInt32()
            
            # 检查serviceUuid是否为游戏服务器标识
            GAME_SERVICE_UUID = 0x0000000063335342
            if service_uuid != GAME_SERVICE_UUID:
                logger.debug(f"跳过serviceId为 {service_uuid} 的NotifyMsg")
                return None
                
            logger.debug(f"methodId={method_id} isZstdCompressed={is_zstd_compressed}")
            
            # 读取剩余数据
            msg_payload = reader.readRemaining()
            
            # 解压缩
            if is_zstd_compressed:
                try:
                    dctx = zstd.ZstdDecompressor()
                    msg_payload = dctx.decompress(msg_payload, max_output_size=1024*1024)
                    logger.debug(f"Notify解压缩成功, 解压缩后数据长度: {len(msg_payload)}")
                except Exception as e:
                    logger.debug(f"Notify zstd解压缩失败: {e}")
                    
            # 根据methodId处理
            SYNC_CONTAINER_DATA_METHOD = 0x00000015
            SyncNearEntities = 0x00000006
            SyncContainerDirtyData = 0x00000016
            SyncNearDeltaInfo = 0x0000002d
            SyncToMeDeltaInfo = 0x0000002e
            
            if method_id == SYNC_CONTAINER_DATA_METHOD:
                logger.debug('SyncContainerData数据包')
                logger.debug(f"发现SyncContainerData数据包 (serviceUuid: 0x{service_uuid:016x}, methodId: 0x{method_id:08x})")
                
                # 解析protobuf数据
                sync_data = SyncContainerData()
                sync_data.ParseFromString(msg_payload)
                
                # 通过回调函数传递数据，而不是直接处理
                if self.callback:
                    self.callback({'v_data': sync_data.VData})


            elif method_id == SyncNearEntities:
                logger.debug("发现SyncNearEntities数据包")
            elif method_id == SyncContainerDirtyData:
                logger.debug("发现SyncContainerDirtyData数据包")
            elif method_id == SyncNearDeltaInfo:
                logger.debug("发现SyncNearDeltaInfo数据包")
            elif method_id == SyncToMeDeltaInfo:
                logger.debug("发现SyncToMeDeltaInfo数据包")
            else:
                logger.debug(f"跳过methodId为 {method_id} 的NotifyMsg")
                
        except Exception as e:
            logger.debug(f"处理Notify消息失败: {e}")
            
        return None
        
    def _process_frame_down_msg(self, reader: BinaryReader, is_zstd_compressed: bool) -> Optional[Dict[str, Any]]:
        """处理FrameDown消息, 使用流式读取"""
        try:
            # 读取服务器序列号
            server_sequence_id = reader.readUInt32()
            
            if reader.remaining() == 0:
                return None
                
            # 读取嵌套数据包
            nested_packet = reader.readRemaining()
            
            # 解压缩
            if is_zstd_compressed:
                try:
                    dctx = zstd.ZstdDecompressor()
                    nested_packet = dctx.decompress(nested_packet, max_output_size=1024*1024)
                    logger.debug(f"FrameDown解压缩成功, 解压缩后数据长度: {len(nested_packet)}")
                except Exception as e:
                    logger.debug(f"FrameDown zstd解压缩失败: {e}")
                    # 继续处理原始数据
                    
            logger.debug(f"处理FrameDown嵌套数据包, 服务器序列号: {server_sequence_id}")
            
            # 递归处理嵌套数据包
            return self._parse_sync_container_data(nested_packet)
            
        except Exception as e:
            logger.debug(f"处理FrameDown消息失败: {e}")
            
        return None

    def _cleanup_loop(self):
        """定时清理循环"""
        while self.is_running:
            try:
                time.sleep(10)  # 每10秒清理一次
                self._cleanup_expired_cache()
            except Exception as e:
                logger.debug(f"清理缓存时发生错误: {e}")
                
    def _cleanup_expired_cache(self):
        """清理过期的缓存"""
        FRAGMENT_TIMEOUT = 30  # 30秒超时
        
        with self.tcp_lock:
            current_time = time.time()
            
            # 清理过期的TCP缓存
            expired_seqs = []
            for seq in self.tcp_cache:
                if current_time - self.tcp_last_time > FRAGMENT_TIMEOUT:
                    expired_seqs.append(seq)
                    
            for seq in expired_seqs:
                del self.tcp_cache[seq]
                
            if expired_seqs:
                logger.debug(f"清理了 {len(expired_seqs)} 个过期的TCP缓存项")
                
            # 检查连接超时
            if self.tcp_last_time and current_time - self.tcp_last_time > FRAGMENT_TIMEOUT:
                logger.warning('无法捕获下一个数据包! 游戏是否已关闭或断开连接?seq: ' + str(self.tcp_next_seq))
                self.current_server = ''
                self._clear_tcp_cache()
