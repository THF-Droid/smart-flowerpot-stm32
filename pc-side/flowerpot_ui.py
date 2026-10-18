# -*- coding: utf-8 -*-
"""
智能花盆控制系统 - 用户操作界面
基于 STM32F4 + FreeRTOS 的串口控制面板
Python 3.11.5 + tkinter + pyserial
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import queue

# ======================== 配置区域 ========================
BAUD_RATES = [9600, 115200, 38400, 19200, 4800]
REFRESH_INTERVAL = 200  # 刷新接收数据的间隔(ms)

# ======================== 串口通信线程 ========================
class SerialThread(threading.Thread):
    """串口通信线程，负责接收数据并放入队列"""
    def __init__(self, ser, data_queue):
        super().__init__(daemon=True)
        self.ser = ser
        self.data_queue = data_queue
        self.running = True

    def run(self):
        while self.running:
            try:
                if self.ser and self.ser.is_open and self.ser.in_waiting > 0:
                    data = self.ser.readline()
                    try:
                        text = data.decode('utf-8', errors='replace').strip()
                        if text:
                            self.data_queue.put(text)
                    except:
                        pass
            except (serial.SerialException, OSError):
                self.data_queue.put("[错误] 串口连接断开!")
                break
            time.sleep(0.05)

    def stop(self):
        self.running = False


# ======================== 主界面 ========================
class FlowerpotUI:
    """智能花盆控制UI"""

    # 按钮颜色配置
    COLOR_ON = "#4CAF50"      # 开启 - 绿色
    COLOR_OFF = "#F44336"     # 关闭 - 红色
    COLOR_NEUTRAL = "#2196F3" # 中性 - 蓝色
    COLOR_SERVO = "#FF9800"   # 舵机 - 橙色
    COLOR_BUZZER = "#9C27B0"  # 蜂鸣器 - 紫色
    COLOR_MODE_ON = "#4CAF50"
    COLOR_MODE_OFF = "#F44336"
    COLOR_DISCONNECT = "#757575"
    COLOR_CONNECT = "#4CAF50"

    def __init__(self, root):
        self.root = root
        self.root.title("智能花盆控制系统 v1.1")
        self.root.geometry("900x680")
        self.root.resizable(True, True)
        self.root.configure(bg="#2C2C2C")

        # 串口对象
        self.ser = None
        self.serial_thread = None
        self.data_queue = queue.Queue()

        # 构建界面
        self._build_serial_bar()
        self._build_control_panel()
        self._build_data_display()
        self._build_status_bar()

        # 启动定时刷新
        self._refresh_display()

        # 窗口关闭时清理
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        # 初始扫描串口
        self._scan_ports()

    # ==================== 串口选择栏 ====================
    def _build_serial_bar(self):
        """构建串口连接栏"""
        frame = tk.Frame(self.root, bg="#3C3C3C", height=50)
        frame.pack(fill=tk.X, padx=5, pady=5)
        frame.pack_propagate(False)

        tk.Label(frame, text="串口:", fg="white", bg="#3C3C3C",
                 font=("微软雅黑", 11)).pack(side=tk.LEFT, padx=(10, 5))

        self.port_combo = ttk.Combobox(frame, width=10, state="readonly",
                                       font=("微软雅黑", 11))
        self.port_combo.pack(side=tk.LEFT, padx=5)

        tk.Button(frame, text="刷新", command=self._scan_ports,
                  bg="#555", fg="white", font=("微软雅黑", 10),
                  activebackground="#666").pack(side=tk.LEFT, padx=3)

        tk.Label(frame, text="波特率:", fg="white", bg="#3C3C3C",
                 font=("微软雅黑", 11)).pack(side=tk.LEFT, padx=(15, 5))

        self.baud_combo = ttk.Combobox(frame, width=8, state="readonly",
                                       values=BAUD_RATES, font=("微软雅黑", 11))
        self.baud_combo.current(0)
        self.baud_combo.pack(side=tk.LEFT, padx=5)

        self.connect_btn = tk.Button(frame, text="连接", command=self._toggle_connect,
                                     bg=self.COLOR_CONNECT, fg="white",
                                     font=("微软雅黑", 11, "bold"), width=8)
        self.connect_btn.pack(side=tk.LEFT, padx=(15, 5))

        # 模式指示灯
        self.mode_label = tk.Label(frame, text="● 自动", fg="#4CAF50", bg="#3C3C3C",
                                   font=("微软雅黑", 11, "bold"))
        self.mode_label.pack(side=tk.RIGHT, padx=15)

        tk.Label(frame, text="当前模式:", fg="#CCC", bg="#3C3C3C",
                 font=("微软雅黑", 10)).pack(side=tk.RIGHT, padx=3)

    # ==================== 控制面板 ====================
    def _build_control_panel(self):
        """构建按钮控制面板"""
        main_frame = tk.Frame(self.root, bg="#2C2C2C")
        main_frame.pack(fill=tk.X, padx=5, pady=2)

        # ---- 模式控制 ----
        mode_frame = tk.LabelFrame(main_frame, text="模式控制",
                                   fg="white", bg="#333",
                                   font=("微软雅黑", 11))
        mode_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=3)

        self.btn_on = tk.Button(mode_frame, text="ON 手动模式",
                                command=lambda: self._send_cmd("ON"),
                                bg=self.COLOR_MODE_ON, fg="white",
                                font=("微软雅黑", 11, "bold"), width=14, height=2)
        self.btn_on.pack(padx=10, pady=(10, 5))

        self.btn_off = tk.Button(mode_frame, text="OFF 自动模式",
                                 command=lambda: self._send_cmd("OFF"),
                                 bg=self.COLOR_MODE_OFF, fg="white",
                                 font=("微软雅黑", 11, "bold"), width=14, height=2)
        self.btn_off.pack(padx=10, pady=(5, 10))

        # ---- 风扇控制 ----
        fan_frame = tk.LabelFrame(main_frame, text="风扇控制",
                                  fg="white", bg="#333",
                                  font=("微软雅黑", 11))
        fan_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=3)

        tk.Button(fan_frame, text="AO 开启风扇",
                  command=lambda: self._send_cmd("AO"),
                  bg=self.COLOR_ON, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(10, 5))

        tk.Button(fan_frame, text="AF 关闭风扇",
                  command=lambda: self._send_cmd("AF"),
                  bg=self.COLOR_OFF, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(5, 10))

        # ---- 补光灯控制 ----
        led_frame = tk.LabelFrame(main_frame, text="补光灯控制",
                                  fg="white", bg="#333",
                                  font=("微软雅黑", 11))
        led_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=3)

        tk.Button(led_frame, text="BO 开启补光灯",
                  command=lambda: self._send_cmd("BO"),
                  bg=self.COLOR_ON, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(10, 5))

        tk.Button(led_frame, text="BF 关闭补光灯",
                  command=lambda: self._send_cmd("BF"),
                  bg=self.COLOR_OFF, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(5, 10))

        # ---- 水泵控制 ----
        pump_frame = tk.LabelFrame(main_frame, text="水泵控制",
                                   fg="white", bg="#333",
                                   font=("微软雅黑", 11))
        pump_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=3)

        tk.Button(pump_frame, text="CO 开启水泵",
                  command=lambda: self._send_cmd("CO"),
                  bg=self.COLOR_ON, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(10, 5))

        tk.Button(pump_frame, text="CF 关闭水泵",
                  command=lambda: self._send_cmd("CF"),
                  bg=self.COLOR_OFF, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(5, 10))

        # ---- 第二行：舵机 + 蜂鸣器 + 快捷控制 ----
        row2 = tk.Frame(self.root, bg="#2C2C2C")
        row2.pack(fill=tk.X, padx=5, pady=2)

        # ---- 舵机控制 ----
        servo_frame = tk.LabelFrame(row2, text="舵机控制 (遮光帘)",
                                    fg="white", bg="#333",
                                    font=("微软雅黑", 11))
        servo_frame.pack(side=tk.LEFT, fill=tk.BOTH, padx=5, pady=3)

        servo_inner = tk.Frame(servo_frame, bg="#333")
        servo_inner.pack(pady=5)

        tk.Button(servo_inner, text="SERVO_0\n遮光帘全开",
                  command=lambda: self._send_cmd("SERVO_0"),
                  bg=self.COLOR_SERVO, fg="white",
                  font=("微软雅黑", 11), width=12, height=2).pack(side=tk.LEFT, padx=3)

        tk.Button(servo_inner, text="SERVO_90\n遮光帘半开",
                  command=lambda: self._send_cmd("SERVO_90"),
                  bg=self.COLOR_SERVO, fg="white",
                  font=("微软雅黑", 11), width=12, height=2).pack(side=tk.LEFT, padx=3)

        tk.Button(servo_inner, text="SERVO_180\n遮光帘全关",
                  command=lambda: self._send_cmd("SERVO_180"),
                  bg=self.COLOR_SERVO, fg="white",
                  font=("微软雅黑", 11), width=12, height=2).pack(side=tk.LEFT, padx=3)

        # ---- 蜂鸣器控制 ----
        buzzer_frame = tk.LabelFrame(row2, text="蜂鸣器",
                                     fg="white", bg="#333",
                                     font=("微软雅黑", 11))
        buzzer_frame.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=3)

        tk.Button(buzzer_frame, text="BUZZER\n测试蜂鸣器",
                  command=lambda: self._send_cmd("BUZZER"),
                  bg=self.COLOR_BUZZER, fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(10, 3))

        # ===== 新增：手动关闭蜂鸣器按钮 =====
        tk.Button(buzzer_frame, text="BZOFF\n关闭蜂鸣器",
                  command=lambda: self._send_cmd("BZOFF"),
                  bg="#757575", fg="white",
                  font=("微软雅黑", 11), width=14, height=2).pack(padx=10, pady=(3, 10))

        # ---- 快捷组合控制 ----
        quick_frame = tk.LabelFrame(row2, text="快捷操作",
                                    fg="white", bg="#333",
                                    font=("微软雅黑", 11))
        quick_frame.pack(side=tk.LEFT, fill=tk.BOTH, padx=5, pady=3, expand=True)

        quick_inner = tk.Frame(quick_frame, bg="#333")
        quick_inner.pack(pady=5)

        tk.Button(quick_inner, text="全部开启",
                  command=self._all_on,
                  bg="#FF5722", fg="white",
                  font=("微软雅黑", 11, "bold"), width=12, height=1).pack(side=tk.LEFT, padx=5)

        tk.Button(quick_inner, text="全部关闭",
                  command=self._all_off,
                  bg="#607D8B", fg="white",
                  font=("微软雅黑", 11, "bold"), width=12, height=1).pack(side=tk.LEFT, padx=5)

        tk.Button(quick_inner, text="切自动模式",
                  command=self._auto_mode,
                  bg="#4CAF50", fg="white",
                  font=("微软雅黑", 11, "bold"), width=12, height=1).pack(side=tk.LEFT, padx=5)

    # ==================== 数据接收区 ====================
    def _build_data_display(self):
        """构建串口数据接收显示区"""
        frame = tk.LabelFrame(self.root, text="串口数据接收 (实时刷新)",
                              fg="white", bg="#333",
                              font=("微软雅黑", 11))
        frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=3)

        # 控制栏
        ctrl = tk.Frame(frame, bg="#333")
        ctrl.pack(fill=tk.X, padx=5, pady=3)

        tk.Button(ctrl, text="清空", command=self._clear_display,
                  bg="#555", fg="white", font=("微软雅黑", 10)).pack(side=tk.RIGHT, padx=3)

        self.auto_scroll_var = tk.BooleanVar(value=True)
        tk.Checkbutton(ctrl, text="自动滚动", variable=self.auto_scroll_var,
                       fg="white", bg="#333", selectcolor="#333",
                       font=("微软雅黑", 10)).pack(side=tk.RIGHT, padx=3)

        # 数据文本区域
        self.data_text = scrolledtext.ScrolledText(
            frame, wrap=tk.WORD, bg="#1E1E1E", fg="#00FF00",
            font=("Consolas", 11), insertbackground="white",
            state=tk.DISABLED
        )
        self.data_text.pack(fill=tk.BOTH, expand=True, padx=3, pady=3)

    # ==================== 状态栏 ====================
    def _build_status_bar(self):
        """底部状态栏"""
        self.status_frame = tk.Frame(self.root, bg="#3C3C3C", height=28)
        self.status_frame.pack(fill=tk.X, side=tk.BOTTOM)
        self.status_frame.pack_propagate(False)

        self.status_label = tk.Label(self.status_frame, text="未连接",
                                     fg="#FF5722", bg="#3C3C3C",
                                     font=("微软雅黑", 10))
        self.status_label.pack(side=tk.LEFT, padx=10)

        self.tx_count_label = tk.Label(self.status_frame, text="发送: 0",
                                       fg="#CCC", bg="#3C3C3C",
                                       font=("微软雅黑", 10))
        self.tx_count_label.pack(side=tk.RIGHT, padx=10)

        self.rx_count_label = tk.Label(self.status_frame, text="接收: 0",
                                       fg="#CCC", bg="#3C3C3C",
                                       font=("微软雅黑", 10))
        self.rx_count_label.pack(side=tk.RIGHT, padx=10)

        self.tx_count = 0
        self.rx_count = 0

    # ==================== 核心功能 ====================
    def _scan_ports(self):
        """扫描可用串口"""
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        if port_list:
            self.port_combo['values'] = port_list
            self.port_combo.current(0)
            self._log_to_display(f"[系统] 发现 {len(port_list)} 个串口: {', '.join(port_list)}")
        else:
            self.port_combo['values'] = ["无可用串口"]
            self.port_combo.current(0)
            self._log_to_display("[系统] 未发现可用串口")

    def _toggle_connect(self):
        """连接/断开串口"""
        if self.ser and self.ser.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        """连接串口"""
        port = self.port_combo.get()
        baud = int(self.baud_combo.get())
        if not port or port == "无可用串口":
            messagebox.showwarning("警告", "请先选择串口!")
            return

        try:
            self.ser = serial.Serial(port, baud, timeout=0.5)
            self._log_to_display(f"[系统] 已连接 {port} @ {baud}bps")
            self.connect_btn.config(text="断开", bg=self.COLOR_DISCONNECT)
            self.status_label.config(text=f"已连接 {port}  {baud}bps", fg="#4CAF50")

            # 启动接收线程
            self.serial_thread = SerialThread(self.ser, self.data_queue)
            self.serial_thread.start()

        except serial.SerialException as e:
            messagebox.showerror("连接失败", f"无法打开串口:\n{e}")
            self._log_to_display(f"[错误] 连接失败: {e}")

    def _disconnect(self):
        """断开串口"""
        if self.serial_thread:
            self.serial_thread.stop()
            self.serial_thread = None

        if self.ser:
            try:
                self.ser.close()
            except:
                pass
            self.ser = None

        self.connect_btn.config(text="连接", bg=self.COLOR_CONNECT)
        self.status_label.config(text="未连接", fg="#FF5722")
        self._log_to_display("[系统] 已断开连接")

    def _send_cmd(self, cmd):
        """发送指令到串口"""
        if not self.ser or not self.ser.is_open:
            self._log_to_display(f"[警告] 未连接串口! 无法发送: {cmd}")
            messagebox.showwarning("未连接", "请先连接串口!")
            return

        try:
            full_cmd = f"{cmd}\r\n"
            self.ser.write(full_cmd.encode('utf-8'))
            self.tx_count += 1
            self.tx_count_label.config(text=f"发送: {self.tx_count}")
            self._log_to_display(f"[发送] >>> {cmd}")

            # 根据指令更新模式指示灯
            if cmd == "ON":
                self.mode_label.config(text="● 手动", fg="#FF5722")
            elif cmd == "OFF":
                self.mode_label.config(text="● 自动", fg="#4CAF50")

        except serial.SerialException as e:
            self._log_to_display(f"[错误] 发送失败: {e}")

    def _all_on(self):
        """全部开启（风扇+灯+水泵）"""
        for cmd in ["ON", "AO", "BO", "CO"]:
            self._send_cmd(cmd)
            time.sleep(0.05)

    def _all_off(self):
        """全部关闭：先切手动→关蜂鸣器+风扇+灯+水泵→切回自动"""
        for cmd in ["ON", "BZOFF", "AF", "BF", "CF", "OFF"]:
            self._send_cmd(cmd)
            time.sleep(0.05)

    def _auto_mode(self):
        """切回自动模式（OFF命令会自动复位所有外设）"""
        self._send_cmd("OFF")

    def _log_to_display(self, text):
        """写入数据到显示区"""
        self.data_text.config(state=tk.NORMAL)
        self.data_text.insert(tk.END, text + "\n")
        if self.auto_scroll_var.get():
            self.data_text.see(tk.END)
        self.data_text.config(state=tk.DISABLED)

    def _clear_display(self):
        """清空数据显示区"""
        self.data_text.config(state=tk.NORMAL)
        self.data_text.delete(1.0, tk.END)
        self.data_text.config(state=tk.DISABLED)

    def _refresh_display(self):
        """定时从队列读取接收数据并显示"""
        try:
            while True:
                data = self.data_queue.get_nowait()
                self.rx_count += 1
                self.rx_count_label.config(text=f"接收: {self.rx_count}")
                self._log_to_display(f"[接收] <<< {data}")

                # 解析AI状态更新模式指示灯
                if "AI:WATER!" in data:
                    self._log_to_display("  ⚠ AI预警: 缺水!")
                elif "AI:LIGHT!" in data:
                    self._log_to_display("  ⚠ AI预警: 光照不足!")
                elif "AI:TEMP!" in data:
                    self._log_to_display("  ⚠ AI预警: 温度异常!")

        except queue.Empty:
            pass

        self.root.after(REFRESH_INTERVAL, self._refresh_display)

    def _on_close(self):
        """窗口关闭时的清理"""
        self._disconnect()
        self.root.destroy()


# ======================== 主入口 ========================
if __name__ == "__main__":
    root = tk.Tk()
    app = FlowerpotUI(root)
    root.mainloop()
