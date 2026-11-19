# STM32F103-W5500-WebFont

基于 STM32F103C8 的 W5500 网口 + 网页字库测试基础工程。作为后续变种功能的基础版本（base），保持简洁，便于派生。

## 功能

- W5500 通过 SPI1 接入，静态 IP `192.168.97.138`，HTTP 服务监听 80 端口
- 网页输入单个汉字，从字库 Flash 读取点阵并以黑白方格显示
- 两片 SPI Flash 共用软件 SPI：字库 Flash（F2_CS）、普通 Flash（F1_CS）

## 目录结构

```
.
├── Core/
│   ├── Inc/              # 应用层头文件
│   ├── Src/              # 应用层源码（main、网络、字库、SPI）
│   ├── ioLibrary/        # WIZnet W5500 驱动
│   ├── AGENTS.md         # AI 协作规则
│   └── WORKLOG.md        # 工作日志（按 AGENTS.md 记录）
├── Drivers/              # STM32 HAL + CMSIS
├── MDK-ARM/              # Keil 工程（NET_test.uvprojx）
├── NET_test.ioc          # STM32CubeMX 配置
└── README.md
```

## 硬件引脚

| 功能 | 引脚 |
|---|---|
| SPI1（W5500）SCK/MISO/MOSI | PA5 / PA6 / PA7 |
| CS_NET（W5500 片选） | PA4 |
| RST（W5500 复位） | PA1 |
| INT（W5500 中断，EXTI0 下降沿） | PA0 |
| 软件 SPI SCK/MISO/MOSI（Flash） | PC15 / PC14 / PC13 |
| F1_CS（普通 Flash） | PA12 |
| F2_CS（字库 Flash） | PA15 |

## 编译与运行

1. 用 Keil 打开 `MDK-ARM/NET_test.uvprojx`。
2. 编译下载到 STM32F103C8（64KB Flash / 20KB RAM）。
3. PC 设置同网段（如 `192.168.97.1`），浏览器访问 `http://192.168.97.138/`。

网络参数在 `Core/Inc/w5500_port.h` 的 `NET_*_ADDR` 宏中修改。

## 关键接口

- `GET /` — 测试页
- `GET /api/status` — 运行状态（JSON）
- `GET /api/font?code=XXXX&size=N` — 字模点阵（GBK 码十六进制，`size` 0=12x12, 1=15x16, 2=24x24, 3=32x32）

## 派生变种

基于本工程创建新功能时，建议：
- 在 `Core/Src` / `Core/Inc` 增加独立模块文件，不改 base 模块；
- 在 `Core/WORKLOG.md` 中以新编号记录改动，不覆盖历史条目；
- 使用 Git 分支管理不同变种，base 分支保持本状态。
