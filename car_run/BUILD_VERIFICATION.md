# TC264 ADS 编译验证

> 以下为历史板端编译记录。圆环/弯道模块拆分后的代码已通过主机测试，尚未重新执行 ADS/TASKING 板端构建；现有 `Debug` 产物不代表此次拆分后的版本。新增模块与构建说明见 [MODULES.md](MODULES.md)。

2026-11-02，通过 ADS 1.10.40 图形工作台完成 `patrol` 工程的 TASKING Debug 编译、汇编和链接，目标为 TC264D（编译器 `-Ctc26xb`）。未执行 Flash 或 Debug 下载。

| 配置 | 结果 | ROM | RAM data + bss |
|---|---|---:|---:|
| 图像调试，CAR_DRIVE_ENABLE=0 | 0 errors，0 warnings | 66,046 字节 | 72,534 字节 |
| 电机控制，CAR_DRIVE_ENABLE=1 | 0 errors，0 warnings | 75,528 字节 | 72,537 字节 |

电机控制配置的链接映射已确认包含 `car_control_tick`、`encoder_dir_init` 和舵机 PWM 转换函数，验证覆盖条件编译中的硬件控制代码。发现并修复了行驶配置下未使用 `cam_manual_th` 的警告，该变量现在只在图像调试配置中声明。

最终 `user/car_config.h` 已恢复 `CAR_DRIVE_ENABLE=0`，且重新构建成功。`Debug/camera.elf` 和 `Debug/camera.hex` 都是此默认配置的产物；名称沿用原工程，内容属于 patrol。

最终 HEX SHA-256：`9183922b14e93d1d7a725594a2bb81caa5eb12bd8c0e38e7880aad930f8877df`。

编译记录在工作区根目录 `inspection/ads-verification/pid-split-monitor.log`；启用电机控制的验证日志与中间产物在 `inspection/ads-verification/pid-split-drive/`。这些中间产物仅用于验证，没有烧录。ADS 打开的是工作区根目录下 `inspection/ads-check` 工作区，其中导入的工程指向本目录。

PID 已独立到 `user/car_pid.c/.h`，循迹识别保留在 `user/car_image.c/.h`；`car_control.c` 负责调度与停车逻辑。拆分后现有主机测试全部通过，ADS 两种配置均已重新编译和链接验证，链接映射确认 `car_control.o` 调用 `car_pid.o` 中的转向与速度计算函数。尚未实车验证电机与编码器方向、舵机限位、速度单位换算及 PID 整定。
