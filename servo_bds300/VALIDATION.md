# 验证记录

日期：2026-10-20。适用于此次拆分的 9 个独立工程。

## 已通过

- 9 个工程的 ADS `.project` / `.cproject` XML 可解析；工程名与目录名一致，无 Eclipse 外部链接目录，包含路径均指向项目内有效文件。
- 原有应用 C 文件通过 GCC C99 语法检查，启用 `-Wall -Wextra -Werror`。本次导入的菜单应用层还通过了 ADS 工程 XML 解析和新增舵机控制模块的 GCC C99 严格编译；完整菜单目标编译仍需 ADS/TASKING。使用真实逐飞设备头文件及硬件模拟层，**这不等同于 TASKING 目标编译**。
- 菜单应用层的 GBK 源文件保持原始字节不变，并将 ADS 项目文本编码设为 GBK，避免中文注释和菜单字符串按 UTF-8 解码后出现乱码。
- 运行实际电机应用控制循环的主机测试：左路测试、两秒自动停止、运行中忽略重新启动命令、32 位毫秒计时回绕、只访问电机 PWM 通道。
- 运行实际舵机应用控制循环的主机测试：按 BDS300 约 300 ms 行程时间自动回中、偏转中忽略重新启动命令、计时回绕、只访问舵机 PWM 通道；验证 1000/1500/2000 us 脉宽范围。导入菜单后，主入口改为菜单循环，舵机由菜单 Servo 参数驱动。
- 摄像头实际中断处理代码测试：每帧开始清除旧完成标志，仅完整帧触发快照暂停，暂停期间不重启 DMA 采集，发送后可恢复，以及 UART 回调为空时跳过调用。
- 源工程 1237 个原有文件的 SHA-256 与拆分前记录一致。此次新建的辅助脚本不计入原有文件集合。

## 目标构建限制

已使用本机 AURIX Development Studio 1.10.40 尝试导入和批量构建。9 个工程可被导入识别，但 ADS 的 Project Booster 在无界面模式报 `Workbench has not been created yet` / `NoClassDefFoundError: org/eclipse/cdt/ui/CUIPlugin`，未执行目标构建。

随后尝试直接调用同一 ADS 附带的 TASKING v1.1r8 工具链，首个工程即被许可证阻止：`ctc F104: License does not support running as standalone. Please use the integrated development environment.` 其余工程没有继续重复尝试。

**尚未完成 TC264 目标编译和链接，没有交付声称已构建成功的 ELF/HEX，也尚未进行实板测试。** 请在 ADS 图形界面中导入所需工程，选择 Debug，执行 Clean Project 和 Build Project；确认无错误后烧录该工程新生成的固件。

工程继承 TC26xD B-Step / tc26xb / bga292、TASKING、C99 配置。芯片和封装需要与实物匹配。设备供电、驱动板接口及机械限位按各工程 README 核对。

## 复查

整套目录中的 `checks/run_checks.py` 可用 Python + PATH 中的 GCC 重跑主机检查；脚本使用本地 mocks 和各工程自带头文件，不依赖原工程进行编译。若原工程仍在原位置，会额外复查原文件哈希。

`checks` 内的 EXE 是 Windows 主机测试程序，不能烧录到 TC264。目标构建尝试记录保存在 `checks/ads_build.log` 和 `checks/build_results.json`；`checks/direct_tasking_build.py` 仅适合拥有独立调用许可的 TASKING 环境。本机应在 ADS 界面构建。
