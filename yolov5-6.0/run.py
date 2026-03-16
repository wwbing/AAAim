import win32api
import win32con
import win32gui
import ctypes
from ctypes import wintypes
import numpy as np
import torch
import cv2
import time
from datetime import datetime
from PIL import ImageGrab
from models.experimental import attempt_load
from utils.general import non_max_suppression, scale_coords

# 导入DLL鼠标控制器
print("正在检查DLL鼠标控制器模块...")
try:
    from dll_mouse_controller import init_dll_mouse_controller, move_mouse_dll, cleanup_dll_mouse_controller
    DLL_MOUSE_AVAILABLE = True
    print("✓ DLL鼠标控制器模块导入成功")
except ImportError as e:
    print(f"✗ 无法导入DLL鼠标控制器: {e}")
    print("请确保 dll_mouse_controller.py 文件存在且无语法错误")
    DLL_MOUSE_AVAILABLE = False
except Exception as e:
    print(f"✗ 导入DLL鼠标控制器时发生未知错误: {e}")
    DLL_MOUSE_AVAILABLE = False

# 导入配置
try:
    from mouse_config import get_current_config
    # 获取当前配置
    mouse_config = get_current_config()
    MOVE_MODE = mouse_config["move_mode"]
    SENSITIVITY_FACTOR = mouse_config["sensitivity_factor"]
    DLL_PATH = mouse_config["dll_path"]
    SMOOTH_MOVE_CONFIG = mouse_config["smooth_config"]
    PRECISE_MOVE_CONFIG = mouse_config["precise_config"]
    DEBUG_CONFIG = mouse_config["debug_config"]
    SAFETY_CONFIG = mouse_config["safety_config"]
    print(f"已加载配置: {MOVE_MODE} 模式, 灵敏度: {SENSITIVITY_FACTOR}")
except ImportError:
    print("未找到配置文件，使用默认配置")
    # 移动模式配置
    # 使用说明:
    # 1. MOVE_MODE: 设置为 "smooth" 使用平滑移动，"precise" 使用精确移动，"dll" 使用DLL移动
    # 2. 平滑移动适合一般使用，模拟人类移动轨迹
    # 3. 精确移动适合游戏环境，使用相对移动和灵敏度补偿
    # 4. DLL移动适合被反作弊检测的游戏环境，需要ddll64.dll文件
    # 5. 可以通过修改配置参数来调整移动行为
    MOVE_MODE = "dll"  # "smooth" 为平滑移动, "precise" 为精确移动, "dll" 为DLL移动
    SENSITIVITY_FACTOR = 1.0  # 灵敏度补偿因子，用于游戏环境
    DLL_PATH = "ddll64.dll"  # DLL文件路径
    
    # 平滑移动参数配置
    SMOOTH_MOVE_CONFIG = {
        "min_steps": 10,        # 最小移动步数
        "max_steps": 50,        # 最大移动步数
        "step_factor": 10,      # 步数计算因子 (distance / step_factor)
        "offset_range_factor": 0.2,  # 控制点偏移范围因子
        "max_offset": 50,       # 最大偏移距离
        "jitter_range": 1,      # 随机抖动范围
        "delay_start_end": (0.008, 0.015),  # 开始和结束阶段延迟范围
        "delay_middle": (0.003, 0.008),     # 中间阶段延迟范围
        "direct_move_threshold": 5  # 小于此距离时直接移动
    }
    
    # 精确移动参数配置
    PRECISE_MOVE_CONFIG = {
        "use_relative_move": True,  # 是否使用相对移动
        "fallback_to_absolute": True  # 失败时是否回退到绝对移动
    }
    
    # 调试配置
    DEBUG_CONFIG = {
        "enable_movement_logging": True,
        "enable_performance_monitoring": True,
        "log_level": "INFO"
    }
    
    # 安全配置
    SAFETY_CONFIG = {
        "max_move_distance": 1000,
        "min_move_interval": 0.001,
        "enable_boundary_check": True,
        "screen_margin": 10
    }

# 设置DPI感知，解决高DPI缩放问题
try:
    # 设置进程DPI感知
    ctypes.windll.shcore.SetProcessDpiAwareness(1)  # PROCESS_DPI_AWARE
except:
    try:
        # 备用方法
        ctypes.windll.user32.SetProcessDPIAware()
    except:
        pass

# 获取DPI缩放因子
def get_dpi_scale():
    try:
        # 获取主显示器的DPI
        hdc = ctypes.windll.user32.GetDC(0)
        dpi_x = ctypes.windll.gdi32.GetDeviceCaps(hdc, 88)  # LOGPIXELSX
        dpi_y = ctypes.windll.gdi32.GetDeviceCaps(hdc, 90)  # LOGPIXELSY
        ctypes.windll.user32.ReleaseDC(0, hdc)
        
        # 标准DPI是96，计算缩放因子
        scale_x = dpi_x / 96.0
        scale_y = dpi_y / 96.0
        
        print(f"DPI: {dpi_x}x{dpi_y}, 缩放因子: {scale_x:.2f}x{scale_y:.2f}")
        return scale_x, scale_y
    except:
        print("无法获取DPI信息，使用默认缩放因子1.0")
        return 1.0, 1.0

# 模型路径
pt_path = r'C:\Users\jiahao\Desktop\JIAHAO\tools\Aimlab_aim\yolov5-6.0\best.pt'

def run():
    global MOVE_MODE
    
    # 初始化DLL鼠标控制器（如果使用DLL模式）
    if MOVE_MODE == "dll":
        print("\n=== DLL模式初始化 ===")
        print(f"配置的DLL模式: {MOVE_MODE}")
        print(f"DLL可用性: {DLL_MOUSE_AVAILABLE}")
        print(f"DLL路径: {DLL_PATH}")
        
        if not DLL_MOUSE_AVAILABLE:
            print("\n[错误] DLL鼠标控制器模块导入失败！")
            print("可能的原因:")
            print("1. dll_mouse_controller.py 文件不存在")
            print("2. 缺少必要的依赖库")
            print("3. Python环境配置问题")
            print("\n程序将退出，请检查DLL模块后重试")
            input("按回车键退出...")
            return
            
        print("\n正在初始化DLL鼠标控制器...")
        if not init_dll_mouse_controller(DLL_PATH):
            print("\n[严重错误] DLL鼠标控制器初始化失败！")
            print("可能的原因:")
            print("1. ddll64.dll 文件不存在或损坏")
            print("2. 硬件设备未正确连接")
            print("3. 权限不足（尝试以管理员身份运行）")
            print("4. DLL文件版本不兼容")
            print("\n由于DLL模式是必需的，程序将退出")
            print("请解决上述问题后重新运行程序")
            input("按回车键退出...")
            return
        else:
            print("\n✓ DLL鼠标控制器初始化成功！")
            print("程序已准备就绪，可以开始使用DLL模式")
            print("=== DLL模式初始化完成 ===\n")
    
    # 获取DPI缩放因子
    dpi_scale_x, dpi_scale_y = get_dpi_scale()
    
    # 定义中心检测区域大小
    detection_width = 800  # 检测区域宽度
    detection_height = 600  # 检测区域高度
    
    # 获取实际屏幕尺寸（考虑DPI缩放）
    screen_width = ctypes.windll.user32.GetSystemMetrics(0)  # SM_CXSCREEN
    screen_height = ctypes.windll.user32.GetSystemMetrics(1)  # SM_CYSCREEN
    
    print(f"实际屏幕尺寸: {screen_width}x{screen_height}")
    
    # 计算屏幕中心
    mid_screen_x = screen_width // 2
    mid_screen_y = screen_height // 2
    
    # 设置检测区域为屏幕中心区域
    rect = (mid_screen_x - detection_width//2, 
            mid_screen_y - detection_height//2,
            mid_screen_x + detection_width//2, 
            mid_screen_y + detection_height//2)
    
    print(f"屏幕中心坐标: ({mid_screen_x}, {mid_screen_y})")
    print(f"检测区域: {rect}")
    
    device = 'cuda' if torch.cuda.is_available() else 'cpu'
    print(f"使用设备: {device}")
    
    model = attempt_load(pt_path, map_location=device, inplace=True, fuse=True)
    model.half()  # 半精度推理
    
    # 获取类别名称
    names = model.module.names if hasattr(model, 'module') else model.names
    print(f"检测类别: {names}")

    conf_thres = 0.4  # 置信度阈值
    iou_thres = 0.5   # IOU阈值
    print("模型加载完成，开始运行")
    print(f"置信度阈值: {conf_thres}, IOU阈值: {iou_thres}")
    print("按鼠标中键切换开关，按鼠标左键进行检测")
    print("按ESC键退出程序")
    
    # 用于计算FPS
    frame_count = 0
    start_time = time.time()
    
    # 调试模式标志
    debug_mode = False
    
    # 显示窗口设置
    window_name = "实时截图中心区域"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(window_name, 400, 400)
    
    # 中心区域大小设置
    center_region_size = 300  # 中心区域的边长（像素）
    
    # 轨迹相关变量
    target_x_global = None
    target_y_global = None
    
    while True:
        # 实时捕获并显示屏幕中心区域
        center_x1 = mid_screen_x - center_region_size // 2
        center_y1 = mid_screen_y - center_region_size // 2
        center_x2 = mid_screen_x + center_region_size // 2
        center_y2 = mid_screen_y + center_region_size // 2
        
        # 捕获中心区域
        center_rect = (center_x1, center_y1, center_x2, center_y2)
        center_im = ImageGrab.grab(bbox=center_rect)
        center_img = np.array(center_im)
        center_img_bgr = cv2.cvtColor(center_img, cv2.COLOR_RGB2BGR)
        
        # 获取图像尺寸用于后续计算
        h, w = center_img_bgr.shape[:2]
        center_h, center_w = h // 2, w // 2
        
        # 绘制鼠标移动轨迹
        if target_x_global is not None and target_y_global is not None:
            # 获取当前鼠标位置
            current_mouse_pos = win32api.GetCursorPos()
            current_mouse_x, current_mouse_y = current_mouse_pos
            
            # 将屏幕坐标转换为窗口内的相对坐标
            # 当前鼠标位置在窗口中的坐标
            if center_x1 <= current_mouse_x <= center_x2 and center_y1 <= current_mouse_y <= center_y2:
                mouse_in_window_x = current_mouse_x - center_x1
                mouse_in_window_y = current_mouse_y - center_y1
            else:
                # 如果鼠标不在显示区域内，使用窗口中心作为起点
                mouse_in_window_x = center_w
                mouse_in_window_y = center_h
            
            # 目标位置在窗口中的坐标
            if center_x1 <= target_x_global <= center_x2 and center_y1 <= target_y_global <= center_y2:
                target_in_window_x = int(target_x_global - center_x1)
                target_in_window_y = int(target_y_global - center_y1)
                
                # 绘制从当前鼠标位置到目标位置的红色轨迹线
                cv2.line(center_img_bgr, 
                        (int(mouse_in_window_x), int(mouse_in_window_y)), 
                        (target_in_window_x, target_in_window_y), 
                        (0, 0, 255), 3)  # 红色线，线宽3
                
                # 在目标位置绘制一个红色圆圈
                cv2.circle(center_img_bgr, (target_in_window_x, target_in_window_y), 8, (0, 0, 255), 2)
                
                # 显示距离信息
                distance = ((target_x_global - current_mouse_x) ** 2 + (target_y_global - current_mouse_y) ** 2) ** 0.5
                cv2.putText(center_img_bgr, f"Distance: {distance:.1f}px", 
                           (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        
        # 显示调试模式状态
        mode_text = "Debug: ON" if debug_mode else "Debug: OFF"
        cv2.putText(center_img_bgr, mode_text, (10, h - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        
        # 显示图像
        cv2.imshow(window_name, center_img_bgr)
        
        # 检查按键
        key = cv2.waitKey(1) & 0xFF
        if key == 27:  # ESC键退出
            break
        
        if (win32api.GetAsyncKeyState(0x04) & 0x8000) > 0:  # 鼠标中键控制开关
            debug_mode = not debug_mode
            print(f"调试模式: {'开启' if debug_mode else '关闭'}")
            # 等待一段时间，防止反复切换
            time.sleep(0.5)

        # 当调试模式开启时，自动进行目标检测和鼠标移动
        if debug_mode:
            # 记录当前时间
            current_time = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            
            # 捕获屏幕
            capture_start = time.time()
            im = ImageGrab.grab(bbox=rect)
            img0 = np.array(im)
            capture_time = time.time() - capture_start
            
            if debug_mode:
                print(f"\n[{current_time}] 捕获屏幕耗时: {capture_time*1000:.2f}ms")
            
            # 预处理图像
            preprocess_start = time.time()
            img = cv2.resize(img0, (640, 640))  # 将图像调整为模型输入大小
            img = img.transpose((2, 0, 1))  # 通道变换
            img = np.ascontiguousarray(img) / 255.0  # 归一化
            img = torch.from_numpy(img).unsqueeze(0).to(device).half()  # 转为tensor
            preprocess_time = time.time() - preprocess_start
            
            if debug_mode:
                print(f"[{current_time}] 预处理图像耗时: {preprocess_time*1000:.2f}ms")
            
            # 模型推理
            inference_start = time.time()
            pred = model(img, augment=False)[0]
            pred = non_max_suppression(pred, conf_thres, iou_thres)
            inference_time = time.time() - inference_start
            
            if debug_mode:
                print(f"[{current_time}] 模型推理耗时: {inference_time*1000:.2f}ms")
            
            # 处理检测结果
            postprocess_start = time.time()
            for i, det in enumerate(pred):
                if det is not None and len(det):
                    det[:, :4] = scale_coords(img.shape[2:], det[:, :4], img0.shape).round()  # 重新缩放框
                    
                    if debug_mode:
                        print(f"[{current_time}] 检测到 {len(det)} 个目标:")
                        print("-" * 50)
                        print(f"{'类别':<10}{'置信度':<10}{'坐标 (x1,y1,x2,y2)':<25}{'中心点':<15}{'距离中心':<10}")
                        print("-" * 50)
                    
                    target_x, target_y = get_target(det, mid_screen_x, mid_screen_y, names, debug_mode, rect)  # 自定义目标选择函数
                    
                    # 更新全局目标坐标，用于轨迹显示
                    target_x_global = target_x
                    target_y_global = target_y
                    
                    if debug_mode:
                        # 计算目标与屏幕中心的距离
                        distance = ((target_x - mid_screen_x) ** 2 + (target_y - mid_screen_y) ** 2) ** 0.5
                        print(f"\n[{current_time}] 选择目标坐标: ({target_x:.1f}, {target_y:.1f}), 距离中心: {distance:.1f}像素")
                        
                        # 简单的ASCII可视化
                        visualize_target(target_x, target_y, mid_screen_x, mid_screen_y)
                    
                    # 只在调试模式开启时移动鼠标
                    if debug_mode:
                        # 根据配置选择移动模式
                        if MOVE_MODE == "dll":
                            print(f"[移动] 使用DLL模式移动到: ({target_x}, {target_y})")
                            if not move_mouse_dll(target_x, target_y):
                                print("[移动] ✗ DLL移动失败！这可能表示硬件问题")
                                print("[移动] 建议检查设备连接或重启程序")
                                # 在DLL模式下，如果移动失败，不应该降级，而是报告错误
                                print("[移动] DLL模式下移动失败，请检查硬件设备")
                            else:
                                print(f"[移动] ✓ DLL移动成功")
                        elif MOVE_MODE == "precise":
                            print(f"[移动] 使用精确模式移动到: ({target_x}, {target_y})")
                            move_mouse_precise(target_x, target_y, SENSITIVITY_FACTOR)
                        else:
                            print(f"[移动] 使用平滑模式移动到: ({target_x}, {target_y})")
                            move_mouse(target_x, target_y)  # 默认使用平滑移动
                    else:
                        print(f"\n[{current_time}] 检测到目标坐标: ({target_x:.1f}, {target_y:.1f}) - 调试模式关闭，不移动鼠标")
            
            postprocess_time = time.time() - postprocess_start
            total_time = capture_time + preprocess_time + inference_time + postprocess_time
            
            # 计算FPS
            frame_count += 1
            elapsed_time = time.time() - start_time
            if elapsed_time >= 1.0:  # 每秒更新一次FPS
                fps = frame_count / elapsed_time
                if debug_mode:
                    print(f"[{current_time}] FPS: {fps:.1f}, 总处理时间: {total_time*1000:.2f}ms")
                frame_count = 0
                start_time = time.time()

    # 清理DLL鼠标控制器
    if MOVE_MODE == "dll" and DLL_MOUSE_AVAILABLE:
        cleanup_dll_mouse_controller()
        print("DLL鼠标控制器已清理")
    
    # 清理窗口
    cv2.destroyAllWindows()
    print("程序已退出")

def get_target(det, mid_screen_x, mid_screen_y, names, debug_mode, rect):
    aims = []
    for *xyxy, conf, cls in reversed(det):
        # 计算边界框的中心点（相对于检测区域）
        center_x_relative = (xyxy[0] + xyxy[2]) / 2
        center_y_relative = (xyxy[1] + xyxy[3]) / 2
        
        # 转换为绝对屏幕坐标
        center_x_absolute = center_x_relative + rect[0]
        center_y_absolute = center_y_relative + rect[1]
        
        # 计算与屏幕中心的距离
        distance = ((center_x_absolute - mid_screen_x) ** 2 + (center_y_absolute - mid_screen_y) ** 2) ** 0.5
        
        # 添加到目标列表
        aims.append((center_x_absolute, center_y_absolute, conf.item(), int(cls.item()), distance, xyxy))
        
        if debug_mode:
            # 显示每个检测到的目标的详细信息
            cls_name = names[int(cls.item())] if names else f"类别{int(cls.item())}"
            print(f"{cls_name:<10}{conf.item():.4f}   ({xyxy[0]:.1f},{xyxy[1]:.1f},{xyxy[2]:.1f},{xyxy[3]:.1f})   ({center_x_absolute:.1f},{center_y_absolute:.1f})   {distance:.1f}")

    if aims:
        # 选择离屏幕中心最近的目标
        target = min(aims, key=lambda x: x[4])  # x[4]是距离
        return target[0], target[1]  # 返回绝对屏幕坐标
    
    if debug_mode and not aims:
        print("未检测到目标，返回屏幕中心坐标")
    
    return mid_screen_x, mid_screen_y  # 默认返回屏幕中心

def visualize_target(target_x, target_y, mid_screen_x, mid_screen_y, width=30, height=15):
    """简单的ASCII可视化，显示目标相对于屏幕中心的位置"""
    # 创建一个表示屏幕的字符矩阵
    screen = [[' ' for _ in range(width)] for _ in range(height)]
    
    # 计算目标在可视化屏幕上的相对位置
    center_col = width // 2
    center_row = height // 2
    
    # 计算目标相对于中心的偏移比例
    offset_x = (target_x - mid_screen_x) / (mid_screen_x / 2)  # 假设屏幕宽度的一半是最大偏移
    offset_y = (target_y - mid_screen_y) / (mid_screen_y / 2)  # 假设屏幕高度的一半是最大偏移
    
    # 将偏移映射到可视化屏幕的坐标
    target_col = int(center_col + offset_x * (width // 4))
    target_row = int(center_row + offset_y * (height // 4))
    
    # 确保坐标在屏幕范围内
    target_col = max(0, min(width - 1, target_col))
    target_row = max(0, min(height - 1, target_row))
    
    # 在屏幕上标记中心点和目标点
    screen[center_row][center_col] = '+'
    screen[target_row][target_col] = 'X'
    
    # 打印可视化屏幕
    print("\n屏幕可视化 (+ 为屏幕中心, X 为目标位置):")
    for row in screen:
        print(''.join(row))

import math
import random

def move_mouse(target_x, target_y):
    """使用平滑轨迹移动鼠标到目标位置"""
    # 获取当前鼠标位置
    current_pos = win32api.GetCursorPos()
    start_x, start_y = current_pos[0], current_pos[1]
    
    print(f"移动前鼠标位置: ({start_x}, {start_y})")
    print(f"目标位置: ({int(target_x)}, {int(target_y)})")
    
    # 计算移动距离
    distance = math.sqrt((target_x - start_x)**2 + (target_y - start_y)**2)
    
    # 如果距离很小，直接移动
    if distance < SMOOTH_MOVE_CONFIG["direct_move_threshold"]:
        try:
            win32api.SetCursorPos((int(target_x), int(target_y)))
            print(f"直接移动到目标位置 (距离: {distance:.1f}px)")
            return
        except Exception as e:
            print(f"直接移动失败: {e}")
            return
    
    # 生成贝塞尔曲线轨迹
    trajectory = generate_bezier_trajectory(start_x, start_y, target_x, target_y, distance)
    
    # 沿轨迹移动鼠标
    move_along_trajectory(trajectory)
    
    # 获取移动后的位置
    final_pos = win32api.GetCursorPos()
    print(f"移动后鼠标位置: ({final_pos[0]}, {final_pos[1]}) (距离: {distance:.1f}px)")

def generate_bezier_trajectory(start_x, start_y, end_x, end_y, distance):
    """生成贝塞尔曲线轨迹点"""
    config = SMOOTH_MOVE_CONFIG
    
    # 根据距离调整步数
    steps = max(config["min_steps"], 
                min(config["max_steps"], 
                    int(distance / config["step_factor"])))
    
    # 生成控制点，添加随机性
    mid_x = (start_x + end_x) / 2
    mid_y = (start_y + end_y) / 2
    
    # 添加随机偏移到控制点
    offset_range = min(config["max_offset"], 
                      distance * config["offset_range_factor"])
    control1_x = mid_x + random.uniform(-offset_range, offset_range)
    control1_y = mid_y + random.uniform(-offset_range, offset_range)
    
    trajectory = []
    
    for i in range(steps + 1):
        t = i / steps
        
        # 三次贝塞尔曲线公式
        x = (1-t)**3 * start_x + 3*(1-t)**2*t * control1_x + 3*(1-t)*t**2 * control1_x + t**3 * end_x
        y = (1-t)**3 * start_y + 3*(1-t)**2*t * control1_y + 3*(1-t)*t**2 * control1_y + t**3 * end_y
        
        # 添加微小的随机抖动
        jitter_range = config["jitter_range"]
        jitter_x = random.uniform(-jitter_range, jitter_range)
        jitter_y = random.uniform(-jitter_range, jitter_range)
        
        trajectory.append((int(x + jitter_x), int(y + jitter_y)))
    
    return trajectory

def move_along_trajectory(trajectory):
    """沿轨迹移动鼠标"""
    config = SMOOTH_MOVE_CONFIG
    
    for i, (x, y) in enumerate(trajectory):
        try:
            # 使用SetCursorPos移动
            win32api.SetCursorPos((x, y))
            
            # 动态延迟：开始和结束时较慢，中间较快
            progress = i / len(trajectory)
            if progress < 0.1 or progress > 0.9:
                delay = random.uniform(*config["delay_start_end"])
            else:
                delay = random.uniform(*config["delay_middle"])
            
            time.sleep(delay)
            
        except Exception as e:
            print(f"轨迹移动失败在点({x}, {y}): {e}")
            # 使用备用方法
            try:
                current_pos = win32api.GetCursorPos()
                dx = x - current_pos[0]
                dy = y - current_pos[1]
                win32api.mouse_event(win32con.MOUSEEVENTF_MOVE, dx, dy, 0, 0)
                time.sleep(delay)
            except Exception as e2:
                print(f"备用移动方法也失败: {e2}")
                break

def move_mouse_precise(target_x, target_y, sensitivity_factor=1.0):
    """精确移动模式，用于游戏环境"""
    config = PRECISE_MOVE_CONFIG
    current_pos = win32api.GetCursorPos()
    start_x, start_y = current_pos[0], current_pos[1]
    
    # 计算需要移动的距离
    dx = target_x - start_x
    dy = target_y - start_y
    
    # 应用灵敏度补偿
    dx = int(dx * sensitivity_factor)
    dy = int(dy * sensitivity_factor)
    
    print(f"精确移动: 从({start_x}, {start_y})到({target_x}, {target_y})")
    print(f"相对移动: ({dx}, {dy}), 灵敏度因子: {sensitivity_factor}")
    
    try:
        if config["use_relative_move"]:
            # 使用相对移动，更适合游戏环境
            win32api.mouse_event(win32con.MOUSEEVENTF_MOVE, dx, dy, 0, 0)
        else:
            # 使用绝对移动
            win32api.SetCursorPos((int(target_x), int(target_y)))
        
        # 验证移动结果
        final_pos = win32api.GetCursorPos()
        print(f"移动后位置: ({final_pos[0]}, {final_pos[1]})")
        
    except Exception as e:
        print(f"精确移动失败: {e}")
        # 回退到备用方法
        if config["fallback_to_absolute"]:
            try:
                win32api.SetCursorPos((int(target_x), int(target_y)))
                print("使用绝对移动作为备用方法")
            except Exception as e2:
                print(f"备用移动也失败: {e2}")

if __name__ == "__main__":
    try:
        print("启动Aim辅助程序...")
        run()
    except KeyboardInterrupt:
        print("\n用户中断程序")
        cv2.destroyAllWindows()
    except Exception as e:
        print(f"程序出错: {e}")
        import traceback
        traceback.print_exc()
        cv2.destroyAllWindows()