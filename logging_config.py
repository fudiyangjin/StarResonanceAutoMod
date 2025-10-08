"""
日志配置
"""

import logging
import os
import sys
from datetime import datetime


def setup_logging(level=logging.INFO, debug_mode=False):
    """
    日志配置
    
    Args:
        level: 日志级别
        debug_mode: 是否为调试模式
    """
    # 如果已经配置过，直接返回
    if logging.getLogger().handlers:
        return
    
    # 设置日志级别
    if debug_mode:
        level = logging.DEBUG
    
    # 创建日志目录
    base_dir = os.path.dirname(sys.executable) if getattr(sys, 'frozen', False) else os.path.dirname(os.path.abspath(__file__))
    log_dir = os.path.join(base_dir, "logs")
    if not os.path.exists(log_dir):
        os.makedirs(log_dir)

    # 生成日志文件名（包含时间戳）
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(log_dir, f"star_resonance_{timestamp}.log")
    
    # 配置日志格式
    formatter = logging.Formatter(
        '[%(asctime)s] [%(name)s] [%(levelname)s] %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )
    
    # 控制台处理器
    console_handler = logging.StreamHandler()
    console_handler.setLevel(level)
    console_handler.setFormatter(formatter)
    
    # 文件处理器
    file_handler = logging.FileHandler(log_file, encoding='utf-8')
    file_handler.setLevel(level)
    file_handler.setFormatter(formatter)
    
    # 配置根日志器
    root_logger = logging.getLogger()
    root_logger.setLevel(level)
    root_logger.addHandler(console_handler)
    root_logger.addHandler(file_handler)
    
    # 记录日志配置信息
    logger = logging.getLogger(__name__)
    logger.info(f"日志系统已初始化 - 级别: {logging.getLevelName(level)}")
    logger.info(f"日志文件: {log_file}")


def get_logger(name):
    """
    获取指定名称的日志器
    
    Args:
        name: 日志器名称
    
    Returns:
        Logger实例
    """
    return logging.getLogger(name) 