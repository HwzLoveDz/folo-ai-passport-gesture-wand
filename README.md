# Mote Wand · 魔杖

**Mote Wand（魔杖）** 是基于 FoloToy AI Passport 的可编程手势控制固件。它把手势变成可管理、可扩展的设备宏：通过 BLE HID 键盘连接主机，QMI8658A 为每个宏学习三次六轴轨迹；之后按住 `OK` 重复任一已训练轨迹并松手，魔杖会自动识别对应宏并执行动作。电脑或手机无需安装驱动。

## 设计灵感与视觉语言

**产品概念。** Mote Wand 的灵感来自《哈利·波特》系列中“挥动魔杖完成施法”的想象：让一段空间手势不只被识别，也能成为连接现实动作与数字指令的个人“咒语”。项目借此把 AI Passport 转化为一支可以学习、识别并管理不同手势的数字魔杖。

**UI 风格。** 界面整体采用 **FUI** 视觉语言，以克制的几何框架、等宽字体、状态图表和高对比色彩，塑造紧凑而富有未来感的仪器界面。视觉气质受到设计师 [Nicolas Lopardo](https://www.nicolaslopardo.com/) 未来系统界面作品的启发；本项目的界面布局、图形元素、动画与交互均为原创设计，并针对 AI Passport 的小尺寸屏幕和实体按键重新构建。

> Mote Wand 是独立的开源项目，与《哈利·波特》系列、Nicolas Lopardo 及其参与的工作室或项目不存在隶属、合作或官方授权关系。

## 硬件条件

- 设备背面如图中红圈所示的预留焊盘需焊接 **QMI8658A 六轴传感器**；未安装该芯片时，手势录制与识别功能无法使用。
- 若原机未贴装，可以使用低温锡膏补焊；QMI8658A 紧邻的两颗电容均使用 **100 nF（0.1 μF）滤波/去耦电容**。务必确认芯片方向，并控制热风温度、风量和持续加热时间，避免传感器受热损坏；上电前检查虚焊、连锡和电源对地短路，上电后再确认 I2C 通信。

<p align="center">
  <img src="docs/images/qmi8658a-back-location.jpg" width="420" alt="FoloToy AI Passport 背面 QMI8658A 焊盘位置">
  <br>
  <sub>QMI8658A 位于设备背部红圈处，芯片旁两颗滤波/去耦电容均为 100 nF。</sub>
</p>

## 界面预览

| 主界面 | 启动验证 | 手势管理 |
| :---: | :---: | :---: |
| <img src="simulator/out/current-home.png" width="200" alt="Mote Wand 主界面"> | <img src="simulator/out/current-pin.png" width="200" alt="启动 PIN 界面"> | <img src="simulator/out/current-menu.png" width="200" alt="手势管理界面"> |
| 手势录制 | 识别成功 | 清除确认 |
| <img src="simulator/out/current-recording.png" width="200" alt="手势录制界面"> | <img src="simulator/out/current-success.png" width="200" alt="手势识别成功界面"> | <img src="simulator/out/current-clear-confirm.png" width="200" alt="清除手势确认界面"> |

## 当前功能

- `AUTH_SEQUENCE`：输入本地配置的数字按键序列并发送回车。
- `LOCK_HOST`：向 Windows 主机发送 `Win+L` 锁屏快捷键。
- `NEW_TAB`：向当前前台浏览器发送 `Ctrl+T`；Chrome 中会新建标签页。
- 独立四位启动 PIN（默认 `0000`）：解锁前不启动 BLE 广播，`UP` / `DOWN` 选数字，`OK` 确认。
- 手势管理器：按宏定义名称列出模型，可直接重录或单独清除。
- 每个宏三次录入，并自动定位偏差较大的样本。
- 48 点归一化轨迹与带约束 DTW 识别，允许一定速度差异。
- 多模型自动选择；两个结果过于接近时阻止执行，新模型与已有模型过于相似时拒绝保存。
- BLE HID 加密连接，成功识别后才执行按键动作。
- Kode Mono FUI 界面、BLE RSSI 条状图、完整状态提示音和 90% 音量。
- CW2017 直接读取电量与电压；每 30 秒更新，手势期间自动顺延。
- 手势模板与 BLE 绑定信息持久化到 NVS。

## 使用

1. 每次开机先输入四位启动 PIN：`UP` / `DOWN` 修改当前位，`OK` 确认并进入下一位；已确认数字会自动隐藏。错误时清空重试，正确后才启动 BLE。默认 PIN 为 `0000`。
2. 在电脑或手机中连接 `Mote Wand`，并按屏幕提示保持设备静止以完成陀螺仪零偏校准。
3. 首次使用会自动录入 `AUTH_SEQUENCE`。连续录入三次：按住 `OK`，完成动作后松开。若一个样本明显不同，只需重录该样本；若任意两次都不一致，则重新录入三次。
4. 屏幕显示 `MACRO STANDBY` 后，按住 `OK` 重复任一已训练手势并松开。达到 75% 且结果不含歧义时执行对应动作。
5. 短按 `UP` 进入手势管理器，`UP` / `DOWN` 选择宏，`OK` 进入详情。详情中用 `UP` / `DOWN` 选择 `RE-RECORD` 或 `CLEAR GESTURE`；清除会在菜单内弹出 `CANCEL` / `CLEAR` 二次选择，长按 `UP` 返回或退出。

轨迹需持续约 0.3–2.6 秒。启动 PIN 通过后，重录和清除不再要求旧手势验证；清除只保留菜单内的防误触确认。重录时旧模型会一直保留到新模型三次录入一致且成功写入 NVS；取消、断电或保存失败都不会清除原模型。清除成功或失败会在当前菜单弹窗显示 1 秒，不会跳回主界面。

## 配置动作

启动 PIN 与 `AUTH_SEQUENCE` 已完全分离：首屏使用独立四位 `MOTE_WAND_BOOT_PIN`（默认 `0000`）；电脑密码仍由本地六位 `MOTE_WAND_ACTION_SEQUENCE` 保存。该序列留空也可以正常构建，但 `AUTH_SEQUENCE` 动作会保持禁用。运行以下命令可分别修改；手势命中时固件会在动作序列末尾自动发送回车：

```bash
idf.py menuconfig
```

配置值保存在本地 `sdkconfig` 中，该文件已被 `.gitignore` 排除。不要提交包含真实凭据的构建产物或配置文件。

日常使用可在设备菜单中单独清除手势。需要批量开发/恢复重置时，可把 `One-shot gesture reset epoch` 改为一个比上次更大的整数并重新烧录；固件只会在该数值首次启动时清除全部手势模型，保留 BLE 配对和其他 NVS 数据。

## ⚠️ Flash 操作警告

> **严禁 AI 或自动化工具对设备 Flash 执行任何形式的不可逆操作，除非用户明确知道自己在做什么，并针对具体目标、影响和恢复风险单独授权。**

不可逆操作包括但不限于：全片擦除、烧写 eFuse、启用或更改 Secure Boot / Flash Encryption、写入或销毁密钥、覆盖未经核验的分区表，以及清除 NVS、蓝牙配对、手势模型或校准数据。普通的“烧录”授权只覆盖已确认兼容且必要的应用区写入，不得被 AI 自动扩大为其他操作。

固件检测到 NVS 空间耗尽或版本不兼容时会停止启动并报告错误，不会自动擦除数据。维护恢复必须先备份并确认分区内容，再由用户针对具体操作单独授权。

## 构建与测试

使用 ESP-IDF 5.5.x：

```bash
tests/run_host_tests.sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

主机测试覆盖同轨迹、噪声、速度变化、错误轨迹、静止、过短录制和异常样本定位。最终阈值仍需在真实持握姿态下验证。

UI 修改优先使用真实 LVGL 主机预览，不需要 ESP-IDF 或连接设备：

```bash
bash simulator/preview.sh
```

首次构建后会复用 WSL 端缓存；预览图输出到 `simulator/out/`。视觉方案确认后再生成嵌入式字体、编译并烧录固件。

## 实现概要

- QMI8658A：共享 I2C0，10 ms 采样；自动探测 `0x6A` / `0x6B`。
- 识别：角速度积分形成姿态路径，线性加速度作为辅助特征。
- 存储：平均模板和 CRC 保存到 NVS；BLE 绑定密钥由 NimBLE 持久化。
- 兼容：旧版单手势模型会在设备端自动迁移为 `AUTH_SEQUENCE`。
- HID：只在 BLE 链路完成加密后发送报告；固件不在日志或界面输出配置值。
- 音频：ES8311 独立任务播放提示音，不阻塞按键回调或 IMU 采样。

## 安全边界

这是便利型自动化工具，不是安全密钥。启动 PIN 只用于阻止随手操作，配置仍存在固件中；`LOCK_HOST` 当前固定为 Windows 的 `Win+L`，`NEW_TAB` 固定为 `Ctrl+T` 且作用于当前前台程序，macOS、Linux 和手机需要按各自快捷键另行定义。BLE 首次配对采用无输入设备常用的 Secure Connections Just Works，不具备 MITM 认证；未启用 Flash Encryption 的设备也可能被物理读取固件中的动作序列。产品化时应启用 Secure Boot、Flash Encryption，并优先使用可撤销的凭据而不是系统主密码。
