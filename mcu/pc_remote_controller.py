"""Desktop remote controller for the tank Bluetooth serial link.

The receiver protocol is intentionally small and human-readable:
W/S/A/D = cardinal movement, Q/E/Z/C = diagonal movement, X = stop.
The controller repeats the current command while a key is held and sends X
when the key is released or the window loses focus.
"""

from __future__ import annotations

import queue
import threading
import tkinter as tk
from tkinter import messagebox, ttk

try:
    import serial
    from serial import SerialException
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - exercised by a user installing deps
    serial = None
    SerialException = Exception
    list_ports = None


BAUD_RATE = 9600
COMMAND_PERIOD_MS = 50
KEY_TO_DIRECTION = {
    "w": "w", "up": "w", "s": "s", "down": "s",
    "a": "a", "left": "a", "d": "d", "right": "d",
}
DIRECTION_LABELS = {"w": "前进", "s": "后退", "a": "左转", "d": "右转"}
SINGLE_COMMANDS = {"w": "W", "s": "S", "a": "A", "d": "D"}
DIAGONAL_COMMANDS = {
    ("w", "a"): ("Q", "AW", "左前"),
    ("w", "d"): ("E", "WD", "右前"),
    ("s", "a"): ("Z", "AS", "左后"),
    ("s", "d"): ("C", "SD", "右后"),
}


class TankRemoteApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("TANK REMOTE  /  蓝牙串口遥控")
        self.geometry("920x620")
        self.minsize(820, 540)
        self.configure(bg="#10151c")

        self.serial_port = None
        self.reader_thread: threading.Thread | None = None
        self.reader_stop = threading.Event()
        self.rx_queue: queue.Queue[bytes] = queue.Queue()
        self.held_vertical: set[str] = set()
        self.held_horizontal: set[str] = set()
        self.vertical_order: list[str] = []
        self.horizontal_order: list[str] = []
        self.active_command = "X"
        self.active_display = "STOP"
        self.active_label = "停止"
        self.last_tx = "--"
        self.tx_count = 0
        self.rx_count = 0
        self.port_var = tk.StringVar()
        self.connection_var = tk.StringVar(value="未连接")
        self.command_var = tk.StringVar(value="STOP")
        self.command_detail_var = tk.StringVar(value="等待按键")
        self.tx_var = tk.StringVar(value="TX  --")
        self.rx_var = tk.StringVar(value="RX  0")
        self.port_state_var = tk.StringVar(value="串口关闭")

        self._build_style()
        self._build_ui()
        self._bind_keys()
        self._refresh_ports()
        self.after(30, self._poll_rx)
        self.after(COMMAND_PERIOD_MS, self._command_tick)
        self.protocol("WM_DELETE_WINDOW", self._close)

    def _build_style(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#10151c")
        style.configure("Panel.TFrame", background="#171e27")
        style.configure("TLabel", background="#10151c", foreground="#d6e0eb")
        style.configure("Muted.TLabel", background="#171e27", foreground="#8190a0")
        style.configure("Title.TLabel", background="#10151c", foreground="#f3f7fb",
                        font=("Segoe UI", 22, "bold"))
        style.configure("PanelTitle.TLabel", background="#171e27", foreground="#f3f7fb",
                        font=("Segoe UI", 11, "bold"))
        style.configure("TButton", padding=(12, 7), font=("Segoe UI", 10, "bold"))
        style.configure("Accent.TButton", background="#1e9e74", foreground="white")
        style.map("Accent.TButton", background=[("active", "#29bd8b")])
        style.configure("Danger.TButton", background="#c84b52", foreground="white")
        style.map("Danger.TButton", background=[("active", "#e15e65")])
        style.configure("TCombobox", fieldbackground="#202a35", background="#202a35",
                        foreground="#e8eef5")

    def _build_ui(self) -> None:
        outer = ttk.Frame(self, padding=26)
        outer.pack(fill="both", expand=True)
        header = ttk.Frame(outer)
        header.pack(fill="x", pady=(0, 20))
        ttk.Label(header, text="TANK REMOTE", style="Title.TLabel").pack(side="left")
        ttk.Label(header, text="USB-TTL  /  BLUETOOTH  /  9600 8N1",
                  foreground="#6f8194", background="#10151c",
                  font=("Consolas", 10, "bold")).pack(side="right", pady=7)

        content = ttk.Frame(outer)
        content.pack(fill="both", expand=True)
        content.columnconfigure(0, weight=0, minsize=280)
        content.columnconfigure(1, weight=1)
        content.rowconfigure(0, weight=1)

        connection = ttk.Frame(content, style="Panel.TFrame", padding=20)
        connection.grid(row=0, column=0, sticky="nsew", padx=(0, 18))
        ttk.Label(connection, text="串口连接", style="PanelTitle.TLabel").pack(anchor="w")
        ttk.Label(connection, text="选择 USB 转 TTL 对应的 COM 口",
                  style="Muted.TLabel").pack(anchor="w", pady=(5, 18))
        ttk.Label(connection, text="端口", style="Muted.TLabel").pack(anchor="w")
        port_row = ttk.Frame(connection, style="Panel.TFrame")
        port_row.pack(fill="x", pady=(6, 14))
        self.port_box = ttk.Combobox(port_row, textvariable=self.port_var, state="readonly")
        self.port_box.pack(side="left", fill="x", expand=True)
        ttk.Button(port_row, text="刷新", command=self._refresh_ports).pack(side="left", padx=(8, 0))
        ttk.Label(connection, text="参数", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(connection, text="9600 baud   ·   8 data bits   ·   no parity   ·   1 stop bit",
                  style="Muted.TLabel", wraplength=235).pack(anchor="w", pady=(6, 20))
        self.connect_button = ttk.Button(connection, text="连接", style="Accent.TButton",
                                         command=self._toggle_connection)
        self.connect_button.pack(fill="x", pady=(0, 10))
        ttk.Label(connection, textvariable=self.port_state_var, style="Muted.TLabel").pack(anchor="w")
        ttk.Separator(connection).pack(fill="x", pady=22)
        ttk.Label(connection, text="通信统计", style="PanelTitle.TLabel").pack(anchor="w")
        ttk.Label(connection, textvariable=self.tx_var, style="Muted.TLabel").pack(anchor="w", pady=(10, 4))
        ttk.Label(connection, textvariable=self.rx_var, style="Muted.TLabel").pack(anchor="w")
        ttk.Label(connection, text="释放按键或按 Space 立即停车", style="Muted.TLabel",
                  wraplength=235).pack(anchor="w", side="bottom")

        drive = ttk.Frame(content, style="Panel.TFrame", padding=24)
        drive.grid(row=0, column=1, sticky="nsew")
        drive.columnconfigure(0, weight=1)
        drive.rowconfigure(2, weight=1)
        ttk.Label(drive, text="驾驶控制", style="PanelTitle.TLabel").grid(row=0, column=0, sticky="w")
        status = ttk.Frame(drive, style="Panel.TFrame")
        status.grid(row=1, column=0, sticky="ew", pady=(10, 18))
        status.columnconfigure(0, weight=1)
        ttk.Label(status, textvariable=self.connection_var, style="Muted.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(status, textvariable=self.command_detail_var, style="Muted.TLabel").grid(row=0, column=1, sticky="e")

        control = ttk.Frame(drive, style="Panel.TFrame")
        control.grid(row=2, column=0, sticky="nsew")
        for i in range(3):
            control.columnconfigure(i, weight=1, uniform="control")
        for i in range(3):
            control.rowconfigure(i, weight=1, uniform="control")
        self._make_drive_button(control, "↑\nW", "w", 0, 1)
        self._make_drive_button(control, "←\nA", "a", 1, 0)
        self._make_drive_button(control, "■\nSTOP", "space", 1, 1, danger=True)
        self._make_drive_button(control, "→\nD", "d", 1, 2)
        self._make_drive_button(control, "↓\nS", "s", 2, 1)
        self.command_badge = tk.Label(drive, textvariable=self.command_var, bg="#10151c",
                                      fg="#39d39a", font=("Consolas", 30, "bold"), pady=13)
        self.command_badge.grid(row=3, column=0, sticky="ew", pady=(20, 0))

    def _make_drive_button(self, parent: ttk.Frame, text: str, key: str, row: int,
                           column: int, danger: bool = False) -> None:
        button = tk.Button(parent, text=text, font=("Segoe UI", 17, "bold"),
                           bg="#273441" if not danger else "#7d3038", fg="#f4f7fa",
                           activebackground="#385267" if not danger else "#aa414b",
                           activeforeground="white", relief="flat", bd=0,
                           cursor="hand2", takefocus=0)
        button.grid(row=row, column=column, sticky="nsew", padx=7, pady=7, ipadx=6, ipady=12)
        if key == "space":
            button.bind("<ButtonPress-1>", lambda _event: self._stop())
            button.bind("<ButtonRelease-1>", lambda _event: self._stop())
        else:
            button.bind("<ButtonPress-1>", lambda _event, k=key: self._press(k))
            button.bind("<ButtonRelease-1>", lambda _event, k=key: self._release(k))

    def _bind_keys(self) -> None:
        for key in KEY_TO_DIRECTION:
            event_key = key.capitalize() if key in {"up", "down", "left", "right"} else key
            self.bind_all(f"<KeyPress-{event_key}>", lambda _event, k=key: self._press(k))
            self.bind_all(f"<KeyRelease-{event_key}>", lambda _event, k=key: self._release(k))
        self.bind_all("<KeyPress-space>", lambda _event: self._stop())
        self.bind_all("<KeyPress-Escape>", lambda _event: self._stop())
        self.bind_all("<FocusOut>", lambda _event: self._stop())

    def _refresh_ports(self) -> None:
        if list_ports is None:
            self.port_box["values"] = []
            self.port_var.set("安装 pyserial 后可用")
            return
        ports = sorted(list_ports.comports(), key=lambda item: item.device)
        values = [f"{item.device}  ·  {item.description}" for item in ports]
        self.port_box["values"] = values
        if values and not self.port_var.get():
            self.port_var.set(values[0])
        elif not values:
            self.port_var.set("未发现串口")

    def _selected_device(self) -> str | None:
        value = self.port_var.get()
        return value.split("  ·  ", 1)[0] if "  ·  " in value else value

    def _toggle_connection(self) -> None:
        if self.serial_port and self.serial_port.is_open:
            self._disconnect()
        else:
            self._connect()

    def _connect(self) -> None:
        device = self._selected_device()
        if not device or device in {"未发现串口", "安装 pyserial 后可用"}:
            messagebox.showwarning("没有可用串口", "请先插入 USB 转 TTL 模块并刷新端口列表。")
            return
        if serial is None:
            messagebox.showerror("缺少依赖", "请在命令行运行:  python -m pip install pyserial")
            return
        try:
            self.serial_port = serial.Serial(device, BAUD_RATE, timeout=0.1,
                                             bytesize=serial.EIGHTBITS,
                                             parity=serial.PARITY_NONE,
                                             stopbits=serial.STOPBITS_ONE)
        except SerialException as exc:
            messagebox.showerror("连接失败", str(exc))
            return
        self.reader_stop.clear()
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_button.configure(text="断开", style="Danger.TButton")
        self.connection_var.set(f"已连接  ·  {device}")
        self.port_state_var.set("串口已打开")
        self._send("X")

    def _disconnect(self) -> None:
        self._stop()
        self.reader_stop.set()
        if self.serial_port:
            try:
                self.serial_port.close()
            except SerialException:
                pass
        self.serial_port = None
        self.connect_button.configure(text="连接", style="Accent.TButton")
        self.connection_var.set("未连接")
        self.port_state_var.set("串口关闭")

    def _reader_loop(self) -> None:
        while not self.reader_stop.is_set():
            port = self.serial_port
            if not port or not port.is_open:
                return
            try:
                data = port.read(port.in_waiting or 1)
            except SerialException:
                return
            if data:
                self.rx_queue.put(data)

    def _poll_rx(self) -> None:
        while True:
            try:
                data = self.rx_queue.get_nowait()
            except queue.Empty:
                break
            self.rx_count += len(data)
            self.rx_var.set(f"RX  {self.rx_count}")
        self.after(30, self._poll_rx)

    def _press(self, key: str) -> None:
        if key == "space":
            self._stop()
            return
        direction = KEY_TO_DIRECTION[key]
        if direction in {"w", "s"}:
            held, order = self.held_vertical, self.vertical_order
        else:
            held, order = self.held_horizontal, self.horizontal_order
        held.add(direction)
        if direction in order:
            order.remove(direction)
        order.append(direction)
        self._apply_held_directions()

    def _release(self, key: str) -> None:
        direction = KEY_TO_DIRECTION[key]
        if direction in {"w", "s"}:
            held, order = self.held_vertical, self.vertical_order
        else:
            held, order = self.held_horizontal, self.horizontal_order
        held.discard(direction)
        if direction in order:
            order.remove(direction)
        self._apply_held_directions()

    @staticmethod
    def _latest_held(held: set[str], order: list[str]) -> str | None:
        for direction in reversed(order):
            if direction in held:
                return direction
        return None

    def _apply_held_directions(self) -> None:
        vertical = self._latest_held(self.held_vertical, self.vertical_order)
        horizontal = self._latest_held(self.held_horizontal, self.horizontal_order)
        if vertical and horizontal:
            wire, display, label = DIAGONAL_COMMANDS[(vertical, horizontal)]
        elif vertical:
            wire, display, label = SINGLE_COMMANDS[vertical], vertical.upper(), DIRECTION_LABELS[vertical]
        elif horizontal:
            wire, display, label = SINGLE_COMMANDS[horizontal], horizontal.upper(), DIRECTION_LABELS[horizontal]
        else:
            self._stop()
            return
        self.active_command = wire
        self.active_display = display
        self.active_label = label
        self._update_command_display()
        self._send(wire)

    def _stop(self) -> None:
        self.held_vertical.clear()
        self.held_horizontal.clear()
        self.vertical_order.clear()
        self.horizontal_order.clear()
        self.active_command, self.active_label = "X", "停止"
        self.active_display = "STOP"
        self._update_command_display()
        self._send("X")

    def _update_command_display(self) -> None:
        self.command_var.set(self.active_display)
        self.command_detail_var.set(self.active_label)
        self.command_badge.configure(fg="#e96b73" if self.active_command == "X" else "#39d39a")

    def _command_tick(self) -> None:
        if self.active_command != "X" and (self.held_vertical or self.held_horizontal):
            self._send(self.active_command)
        self.after(COMMAND_PERIOD_MS, self._command_tick)

    def _send(self, command: str) -> None:
        port = self.serial_port
        if not port or not port.is_open:
            return
        try:
            port.write(command.encode("ascii"))
            self.tx_count += 1
            self.last_tx = command
            self.tx_var.set(f"TX  {self.tx_count}  ·  最后 {command}")
        except SerialException:
            self._disconnect()

    def _close(self) -> None:
        self._stop()
        self._disconnect()
        self.destroy()


if __name__ == "__main__":
    TankRemoteApp().mainloop()
