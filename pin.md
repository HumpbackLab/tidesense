# TideSense（SG2002）原理图引脚连接

## 说明

本文档根据 `picoeye.epro` 中的原理图网络连接整理，核心器件为 `U1: SG2002`，封装为 `QFN-88`，原理图符号另外列出了中心焊盘 `EPAD`。表中的 `NC` 表示未检测到与其他器件或网络相连；“悬空引线”表示图中画出了引线，但引线末端没有网络名或器件连接。

## 1. 主要接口概览

| 功能 | SG2002 引脚 | 原理图网络 | 外围连接 |
| --- | ---: | --- | --- |
| SD0 / TF 卡 | 6–8、10–12 | `SD0_CLK`、`SD0_CMD`、`SD0_D0`、`SD0_D1`、`SD0_D2`、`SD0_D3` | `U6: TF-123-ARP9H17`；卡检测为 `SD0_DET`，未接到 SG2002 的 `SD0_CD` |
| SPI NOR 复用信号 | 20–25 | `SPINOR_HOLD`、`SPINOR_CLK`、`SPINOR_MOSI`、`SPINOR_WP_X`、`SPINOR_MISO`、`SPINOR_CS` | 仅看到网络引出，工程中未发现对应 Flash 器件实例 |
| UART0 | 18–19 | `SOC_UART0_TX/RX` | `U11` 调试串口座 |
| UART1 / IIC0 复用 | 28–29 | `SOC_UART1_TX/RX` | `U10` 调试串口座；符号功能名为 `IIC0_SCL/SDA`，网络名与功能名不一致 |
| SPI2 | 51、53–56 | `SPI2_CS`、`IMU_INT`、`SPI2_MISO/MOSI/SCK` | `U19: LSM6DSOWTR`；52 脚 `SD1_D2` 未连接 |
| 双目 MIPI RX | 72–81 | `MDN0_R/MDP0_R`、`MCN_R/MCP_R`、`MDN0/MDP0`、`MDN1/MDP1`、`MCN/MCP` | `U4/U12: OV9281`；MIPI TX 83–88 未连接 |
| USB 2.0 OTG | 60、69–70 | `VDD1V8_SYS`、`OTG_DP`、`OTG_DN` | `USB1` Type-C；VBUS 接 `VDD5V_VIN` |
| 音频 | 2–4 | `AUD_IN`、`AUD_VREF`、`AUD_OUT` | 音频输入、参考电压和右声道输出网络 |

## 2. U1：SG2002 引脚分配

### 1–22 脚

| Pin | SG2002 符号功能 | 原理图网络 / 状态 | 连接说明 |
| ---: | --- | --- | --- |
| 1 | `VSS18A` | `GND` | 地 |
| 2 | `AUD_AINL_MIC` | `AUD_IN` | 音频输入 |
| 3 | `AUD_AVREF` | `AUD_VREF` | 音频参考电压 |
| 4 | `AUD_AOUTR` | `AUD_OUT` | 音频输出 |
| 5 | `VDD18A` | `VDD1V8_SYS` | 1.8 V 电源 |
| 6 | `SD0_CLK` | `SD0_CLK` | TF 卡时钟 |
| 7 | `SD0_CMD` | `SD0_CMD` | TF 卡命令 |
| 8 | `SD0_D0` | `SD0_D0` | TF 卡数据 D0 |
| 9 | `VDDC` | `VDD0V9_CPU` | 0.9 V 电源 |
| 10 | `SD0_D1` | `SD0_D1` | TF 卡数据 D1 |
| 11 | `SD0_D2` | `SD0_D2` | TF 卡数据 D2 |
| 12 | `SD0_D3` | `SD0_D3` | TF 卡数据 D3 |
| 13 | `VDDIO_SD0` | `VDD3V3_SYS` | SD0 I/O 电源 |
| 14 | `SD0_CD` | `GND` | 被拉低；未接 `U6` 的 `SD0_DET` |
| 15 | `SD0_PWR_EN` | `GPIOA_14` | SD 电源控制 / GPIO 网络 |
| 16 | `VDDC` | `VDD0V9_CPU` | 0.9 V 电源 |
| 17 | `SPK_EN` | `NC` | 未连接 |
| 18 | `UART0_TX` | `SOC_UART0_TX` | 接 `U11` |
| 19 | `UART0_RX` | `SOC_UART0_RX` | 接 `U11` |
| 20 | `SD0_DAT2` | `SPINOR_HOLD` | 复用为 SPI NOR 网络 |
| 21 | `SD0_CLK` | `SPINOR_CLK` | 复用为 SPI NOR 网络 |
| 22 | `SD0_DAT0` | `SPINOR_MOSI` | 复用为 SPI NOR 网络 |

### 23–44 脚

| Pin | SG2002 符号功能 | 原理图网络 / 状态 | 连接说明 |
| ---: | --- | --- | --- |
| 23 | `SD0_DAT3` | `SPINOR_WP_X` | 复用为 SPI NOR 网络 |
| 24 | `SD0_CMD` | `SPINOR_MISO` | 复用为 SPI NOR 网络 |
| 25 | `SD0_DAT1` | `SPINOR_CS` | 复用为 SPI NOR 网络 |
| 26 | `JTAG_TMS` | `NC` | 未连接 |
| 27 | `JTAG_TCK` | `NC` | 未连接 |
| 28 | `IIC0_SCL` | `SOC_UART1_TX` | 接 `U10`；网络名按 UART1 标注 |
| 29 | `IIC0_SDA` | `SOC_UART1_RX` | 接 `U10`；网络名按 UART1 标注 |
| 30 | `AUX0` | `BOOT` | 启动模式网络 |
| 31 | `VDDQ_DRAM` | `VDD1V35_DRAM` | DRAM 电源 |
| 32 | `VDDQ` | `VDD1V35_DRAM` | DRAM 电源 |
| 33 | `VDDQ` | `VDD1V35_DRAM` | DRAM 电源 |
| 34 | `VDDQ_DRAM` | `VDD1V35_DRAM` | DRAM 电源 |
| 35 | `GPIO_ZQ` | `NC` | 未连接 |
| 36 | `VDDIO_RTC` | `VDD1V8_SYS` | RTC I/O 电源 |
| 37 | `VDDC_RTC` | `VDDC_RTC` | RTC 电源网络 |
| 38 | `PWR_VBAT_DET` | `PWR_DET` | 电源检测 |
| 39 | `PWR_RSTN` | `RSTN` | 电源复位 |
| 40 | `PWR_SEQ1` | `NC`（悬空引线） | 有引线但未标网络名，需复核 |
| 41 | `PWR_SEQ2` | `PWR_SEQ2` | 电源时序网络 |
| 42 | `PTEST` | `GND` | 测试脚接地 |
| 43 | `PWR_WAKEUP` | `SHUTDOWN1` | 接 `U4` 摄像头 `XSHUTDOWN` |
| 44 | `PWR_BUTTON` | `SHUTDOWN2` | 接 `U12` 摄像头 `XSHUTDOWN` |

### 45–66 脚

| Pin | SG2002 符号功能 | 原理图网络 / 状态 | 连接说明 |
| ---: | --- | --- | --- |
| 45 | `XTAL_XIN` | `XTAL_XIN` | 25 MHz 晶振网络 |
| 46 | `XTAL_XOUT` | `XTAL_XOUT` | 25 MHz 晶振网络 |
| 47 | `PWR_GPIO0` | `FSIN` | 双摄像头帧同步网络 |
| 48 | `PWR_GPIO1` | `I2C2_SCL` | 双摄像头 I2C 时钟 |
| 49 | `PWR_GPIO2` | `I2C2_SDA` | 双摄像头 I2C 数据 |
| 50 | `VDDC` | `VDD0V9_CPU` | 0.9 V 电源 |
| 51 | `SD1_D3` | `SPI2_CS` | 接 `U19` 片选 |
| 52 | `SD1_D2` | `NC`（悬空引线） | 有引线但未标网络名，需复核 |
| 53 | `SD1_D1` | `IMU_INT` | 接 `U19` 中断 |
| 54 | `SD1_D0` | `SPI2_MISO` | 接 `U19` |
| 55 | `SD1_CMD` | `SPI2_MOSI` | 接 `U19` |
| 56 | `SD1_CLK` | `SPI2_SCK` | 接 `U19` |
| 57 | `VDDIO_SD1` | `VDD3V3_SYS` | SD1 / SPI2 I/O 电源 |
| 58 | `PWM0_BUCK` | `NC`（悬空引线） | 有引线但未标网络名，需复核 |
| 59 | `ADC1` | `NC` | 未连接 |
| 60 | `USB_VBUS_DET` | `VDD1V8_SYS` | 原理图中直接接 1.8 V，不是 `VDD5V_VIN` |
| 61 | `VDDC` | `VDD0V9_CPU` | 0.9 V 电源 |
| 62 | `EPHY_RXN` | `NC` | 未连接，工程中未见 Ethernet PHY |
| 63 | `EPHY_RXP` | `NC` | 未连接，工程中未见 Ethernet PHY |
| 64 | `EPHY_TXN` | `NC` | 未连接，工程中未见 Ethernet PHY |
| 65 | `EPHY_TXP` | `NC` | 未连接，工程中未见 Ethernet PHY |
| 66 | `VDD33A` | `VDD3V3_SYS` | 3.3 V 模拟电源 |

### 67–89 脚

| Pin | SG2002 符号功能 | 原理图网络 / 状态 | 连接说明 |
| ---: | --- | --- | --- |
| 67 | `EPHY_RTX` | `CPU_CEL` | 接 CPU_CEL 网络；不是 EPHY 差分对 |
| 68 | `VDD18A` | `VDD1V8_SYS` | 1.8 V 电源 |
| 69 | `USB_DP` | `OTG_DP` | 接 Type-C D+ |
| 70 | `USB_DM` | `OTG_DN` | 接 Type-C D− |
| 71 | `VDDC` | `VDD0V9_CPU` | 0.9 V 电源 |
| 72 | `MIPIRX4N` | `MDN0_R` | 摄像头差分信号 |
| 73 | `MIPIRX4P` | `MDP0_R` | 摄像头差分信号 |
| 74 | `MIPIRX3N` | `MCN_R` | 摄像头差分信号 |
| 75 | `MIPIRX3P` | `MCP_R` | 摄像头差分信号 |
| 76 | `MIPIRX2N` | `MDN1` | 摄像头差分信号 |
| 77 | `MIPIRX2P` | `MDP1` | 摄像头差分信号 |
| 78 | `MIPIRX1N` | `MDN0` | 摄像头差分信号 |
| 79 | `MIPIRX1P` | `MDP0` | 摄像头差分信号 |
| 80 | `MIPIRX0N` | `MCN` | 摄像头时钟差分信号 |
| 81 | `MIPIRX0P` | `MCP` | 摄像头时钟差分信号 |
| 82 | `VDD18A_MIPI` | `VDD1V8_SYS` | MIPI 电源 |
| 83 | `MIPI_TXM2` | `NC` | 未连接 |
| 84 | `MIPI_TXP2` | `NC` | 未连接 |
| 85 | `MIPI_TXM1` | `NC` | 未连接 |
| 86 | `MIPI_TXP1` | `NC` | 未连接 |
| 87 | `MIPI_TXM0` | `NC` | 未连接 |
| 88 | `MIPI_TXP0` | `NC` | 未连接 |
| 89 | `EPAD` | `GND` | 中心焊盘接地 |

## 3. 外围器件连接

### 摄像头

- `U4`、`U12` 均为 `OV9281`，共用 `I2C2_SCL`、`I2C2_SDA` 和 `FSIN`。
- `U4` 使用 `MCN/MCP`、`MDN0/MDP0`、`MDN1/MDP1`；`U12` 使用 `MCN_R/MCP_R`、`MDN0_R/MDP0_R`。
- `U4.XSHUTDOWN` 接 `SHUTDOWN1`，`U12.XSHUTDOWN` 接 `SHUTDOWN2`。
- 摄像头电源为 `VDD2V8`、`VDD1V2`、`VDD1V8`，地线均接 `GND`。

### TF 卡

`U6` 的数据、时钟、命令和供电连接为：`DAT2→SD0_D2`、`CD/DAT3→SD0_D3`、`CMD→SD0_CMD`、`CLK→SD0_CLK`、`DAT0→SD0_D0`、`DAT1→SD0_D1`、`VDD→VDD3V3_SYS`。其 `SWITCH` 接 `SD0_DET`，但该网络没有接到 U1 的 14 脚；U1 的 14 脚固定接地。

### USB Type-C

`USB1` 的 VBUS 接 `VDD5V_VIN`，D+/D− 分别接 `OTG_DP/OTG_DN`，CC1、CC2、SBU1、SBU2 在当前原理图中未接；外壳焊脚接地。

### 电源

- `U2: EA3059` 以 `VDD5V_VIN` 为输入，四路开关电源输出网络分别为 `VDD3V3_SYS`、`VDD1V8_SYS`、`VDD1V35_DRAM` 和 `VDD0V9_CPU`。
- `U7: ME6211C28M5G-N` 将 `VDD5V_VIN` 转为 `VDD2V8`。
- `U8: ME6211C18M5G-N` 将 `VDD2V8` 转为 `VDD1V8`；`U9: ME6211C12M5G-N` 将 `VDD2V8` 转为 `VDD1V2`。
- 各电源输出均配置了输入/输出去耦电容；SG2002 的 `VDDC` 实际连接到 `VDD0V9_CPU`。

## 4. 需要复核的原理图事项

1. U1 的 28、29 脚符号功能是 `IIC0_SCL/SDA`，但网络却命名为 `SOC_UART1_TX/RX`，并接到 U10；软件配置应以实际复用功能和芯片手册为准。
2. U1 的 20–25 脚引出 `SPINOR_*` 网络，但当前工程中未找到对应 SPI NOR 器件，需确认 Flash 是否在其他板卡或遗漏于工程。
3. U1 的 40、52、58 脚各有一段未命名悬空引线，建议在原理图中明确标注 `NC` 或补齐实际连接。
4. TF 卡检测网络 `SD0_DET` 未连接到 U1 的 `SD0_CD`，而 U1 的 14 脚接地；如需要卡检测功能，应重新确认连接方案。
5. U1 的 60 脚 `USB_VBUS_DET` 直接接 `VDD1V8_SYS`，应确认这是有意的固定电平设计，而不是漏接 USB VBUS 检测分压。
