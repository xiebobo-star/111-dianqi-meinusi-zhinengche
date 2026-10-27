# 虚拟 BDS300 舵机

这个目录提供一个不需要真实舵机的测试端。TC264 菜单仍由主板的 4 个按键操作，固件把每次舵机参数变化通过 UART0 输出，`virtual_bds300.py` 将它还原成角度并模拟约 300 ms 的运动过程。

## 接线

使用 USB-TTL 转换器连接 TC264 的调试串口：

| TC264 | USB-TTL |
|---|---|
| P14_0 / UART0 TX | RX |
| P14_1 / UART0 RX | TX |
| GND | GND |

USB-TTL 必须使用 3.3 V 电平。虚拟模式不需要把 P21_6 接到实体舵机；若同时接实体舵机，请给舵机单独提供 4.8–6 V 电源，并与 TC264 共地。

串口参数是 **115200，8N1**。固件输出格式固定为：

```text
BDS300 angle=90 min_us=1040 center_us=1540 max_us=2040 enable=1 pulse_us=1540 duty=770
```

`pulse_us` 是 P21_6（ATOM1 CH4）的目标脉宽，`enable=0` 表示关闭输出。虚拟模型按同样的最小/中位/最大脉宽分段换算角度，避免只根据 `angle` 字段“自证”。

## 用主板按键测试屏幕参数

1. 在 ADS 中构建并烧录上级目录的 `TC264_Servo_BDS300` 工程。
2. 打开串口工具确认能看到 `BDS300 ...` 行；或关闭串口工具后运行下面的 Python 监听器（只允许一个程序占用 COM 口）。
3. 屏幕进入“参数设置 → Servo”，按键 1/2 选择项目，按键 3 进入/确认，按键 4 返回或取消。按键定义为：KEY1=P20_6、KEY2=P20_7、KEY3=P11_2、KEY4=P11_3。
4. 修改 `Pulse min / Pulse center / Pulse max / Pulse enable` 或角度。每次确认后，终端会出现一行新的 BDS300 数据，虚拟舵机输出 `target_angle_deg` 与随时间变化的 `current_angle_deg`。

`1040 / 1540 / 2040 us` 和 `Pulse enable=开` 正好是工程默认标定值；在这些值下，0°、90°、180° 分别对应 1040、1540、2040 us。

## 启动

直接监听串口（需要 `pyserial`）：

```powershell
python -m pip install pyserial
python .\Virtual_BDS300\virtual_bds300.py --serial COM5
```

`COM5` 只是示例，必须替换成设备管理器中 USB-TTL 实际显示的端口。先查看端口：

```powershell
python .\Virtual_BDS300\virtual_bds300.py --list-ports
```

如果电脑只识别到一个串口，可以自动选择：

```powershell
python .\Virtual_BDS300\virtual_bds300.py --serial auto
```

若显示 `No serial ports found`，说明 USB-TTL 未连接、驱动未安装，或使用了不匹配的 USB 线；这不是虚拟舵机程序的角度计算错误。确认串口工具没有同时占用该 COM 口。

没有硬件时可用管道验证协议：

```powershell
'BDS300 angle=45 min_us=1040 center_us=1540 max_us=2040 enable=1 pulse_us=1290 duty=645' |
  python .\Virtual_BDS300\virtual_bds300.py
```

输出为一行 JSON，例如 `{"target_angle_deg":45.0,"current_angle_deg":45.0,...}`，便于脚本或 ADS 外部测试程序读取。

## 自检

```powershell
python -m unittest discover -s .\Virtual_BDS300 -p 'test_*.py'
```
