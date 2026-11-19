# 工作日志

> 本文件按 `Core/AGENTS.md` 规则记录对代码库的每一次实质性操作。编号连续，永不重复、永不跳号、永不回改历史条目。若历史记录有误，新增「记录修正」条目说明，而非覆盖原条目。

### [1] 复制 WIZnet ioLibrary W5500 驱动到工程
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：依赖变更
- 触发来源：用户需求原文摘要「使用 D:\Desktop\NET_TEST\net_tsat 做参考，使用当前工程 SPI1 做网口，在 192.168.97.138 建立网页」
- 问题现象 / 需求描述：当前工程无任何以太网控制器驱动，无法实现网口功能。
- 根本原因：原工程仅含 HAL 外设初始化，缺少 W5500 驱动与 TCP/IP 协议栈源码。
- 改动位置：`Core/ioLibrary/Ethernet/wizchip_conf.c/.h`、`Core/ioLibrary/Ethernet/socket.c/.h`、`Core/ioLibrary/Ethernet/W5500/w5500.c/.h`
- 改动内容：从参考工程复制上述 6 个文件到当前工程（原无 → 新增）。
- 影响范围：网络模块依赖；需配合记录 6 增加 Keil 头文件路径与源文件分组。
- 验证方式：PowerShell `Get-ChildItem` 确认 6 个文件及大小；`gcc -fsyntax-only` 语法检查通过。
- 遗留风险 / 待办：未在 Keil/硬件上实际编译运行。
- 关联记录：2、6

### [2] 新增 W5500 移植层（SPI1 / CS_NET / RST / INT）
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：同上
- 问题现象 / 需求描述：需把 ioLibrary 的平台相关回调对接到本工程的 SPI1、CS_NET(PA4)、RST(PA1)、INT(PA0)，并写入静态 IP 192.168.97.138。
- 根本原因：ioLibrary 要求平台提供 CS/SPI/临界区回调及硬件复位；参考工程引脚定义（RST=PA3、INT=PA2）与本工程不同。
- 改动位置：`Core/Inc/w5500_port.h`、`Core/Src/w5500_port.c`
- 改动内容：新增文件。关键实现：
  - `w5500_cs_select/deselect` → `CS_NET_GPIO_Port / CS_NET_Pin`
  - `w5500_spi_readbyte/writebyte/readburst/writeburst` → 阻塞式 `HAL_SPI_*` API
  - `w5500_hw_reset()` → RST 低 10ms / 高 200ms
  - `w5500_port_init()` → 注册回调、`wizchip_init()`、校验 `getVERSIONR()==0x04`、`wizchip_setnetinfo()` 写入 MAC/IP/MASK/GW/DNS、`setSIMR(0xFF)`
  - `HAL_GPIO_EXTI_Callback()` → 置位 `s_int_event`
- 影响范围：`main.c` 调用 `w5500_port_init` / `w5500_int_event_take`；`gpio.c` / `stm32f1xx_it.c` 提供 INT 引脚与中断；`http_server.c` 调用 `w5500_get_link`。
- 验证方式：`gcc -fsyntax-only` 通过；未上板验证。
- 遗留风险 / 待办：未验证 W5500 实际 SPI 通信与 PHY 链路；初始化失败会进入 `Error_Handler()` 死循环。
- 关联记录：1、4、5

### [3] 新增简易 HTTP 测试页服务
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：同上
- 问题现象 / 需求描述：需在 `192.168.97.138:80` 提供网页测试页。
- 根本原因：工程无 HTTP 服务实现。
- 改动位置：`Core/Inc/http_server.h`、`Core/Src/http_server.c`
- 改动内容：新增文件。`HTTP_PAGE` 为静态 HTML（内联 CSS/JS，显示 IP/请求数/运行时间/链路状态，每秒 `fetch('/api/status')`）；`http_server_poll()` 状态机处理 `SOCK_CLOSED/INIT/ESTABLISHED/CLOSE_WAIT`；`http_send()` 分块发送；`http_respond()` 构造响应头；路由 `/`、`/index.html`、`/api/status`、404。另：阻塞式 socket（`socket(...,0)`）、等待 `Sn_IR_SENDOK`、CSS 中 `42%%` 修正为 `42%`。
- 影响范围：`main.c` 调用 `http_server_init` / `http_server_poll`；依赖 `w5500_port.h`、`socket.h`、`w5500.h`。
- 验证方式：`gcc -fsyntax-only` 通过；HTML 字符串人工检查；未上板验证。
- 遗留风险 / 待办：单次 `recv()` 处理一个请求，超长/分片请求可能解析不全；不支持并发连接。
- 关联记录：2、4

### [4] main.c 接入网络初始化与主循环
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：同上
- 问题现象 / 需求描述：原 `main.c` 仅初始化外设后进入空循环，未启动网络。
- 根本原因：缺少网络模块调用入口。
- 改动位置：`Core/Src/main.c`（`USER CODE BEGIN Includes` 行 29-32；`USER CODE BEGIN 2` 行 99-105；`USER CODE BEGIN 3` 行 113-118）
- 改动内容：
  - 修改前：无相关 include 与调用，while 循环为空。
  - 修改后：增加 `#include "w5500_port.h"` / `#include "http_server.h"`；调用 `w5500_port_init()`（失败进 `Error_Handler()`）和 `http_server_init()`；while 中调用 `http_server_poll()`，`w5500_int_event_take()` 非零时翻转 `LED_Pin`。
- 影响范围：程序启动流程；LED(PA8) 指示。
- 验证方式：`gcc -fsyntax-only` 通过；未上板。
- 遗留风险 / 待办：`Error_Handler()` 为死循环，无超时恢复。
- 关联记录：2、3、5

### [5] INT 引脚改为 EXTI0 下降沿并使能中断
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：用户原文「有其他的引脚，中断INT、RST、CS_NET」
- 问题现象 / 需求描述：原工程 INT(PA0) 为上升沿、无上拉，且无 `EXTI0_IRQHandler`，一旦触发会落入 `Default_Handler` 死循环；W5500 INTn 为低有效。
- 根本原因：引脚模式与 W5500 INTn 有效电平不匹配，且缺少中断服务程序。
- 改动位置：`Core/Src/gpio.c`（INT 配置段）；`Core/Src/stm32f1xx_it.c`（`USER CODE BEGIN 1`）
- 改动内容：
  - 修改前：`GPIO_MODE_IT_RISING` + `GPIO_NOPULL`，无 NVIC 显式配置，无 ISR。
  - 修改后：`GPIO_MODE_IT_FALLING` + `GPIO_PULLUP`，`HAL_NVIC_SetPriority(EXTI0_IRQn,1,0)` + `HAL_NVIC_EnableIRQ(EXTI0_IRQn)`；新增 `EXTI0_IRQHandler()` 调用 `HAL_GPIO_EXTI_IRQHandler(INT_Pin)`。
- 影响范围：PA0 中断；`w5500_port.c` 中的 `HAL_GPIO_EXTI_Callback`。
- 验证方式：`gcc -fsyntax-only` 通过；未上板。
- 遗留风险 / 待办：未验证 W5500 实际是否输出 INTn 低脉冲。
- 关联记录：2、4

### [6] Keil 工程增加网络模块配置
- 时间：2026-11-19（本轮对话，精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：新增源文件后需让 Keil 识别。
- 问题现象 / 需求描述：新增的 `.c/.h` 未加入 Keil 工程，无法编译。
- 根本原因：`NET_test.uvprojx` 未包含新文件路径与 include 路径。
- 改动位置：`MDK-ARM/NET_test.uvprojx`（`Cads/VariousControls/IncludePath`、`Groups` 末尾）
- 改动内容：
  - `IncludePath` 增加 `../Core/ioLibrary/Ethernet` 与 `../Core/ioLibrary/Ethernet/W5500`。
  - `Groups` 新增 `Network/W5500` 分组，含 `w5500_port.c`、`http_server.c`、`wizchip_conf.c`、`socket.c`、`w5500.c`。
- 影响范围：编译构建。
- 验证方式：PowerShell `[xml]` 解析通过（输出 `XML OK`）；未在 Keil 中实际编译。
- 遗留风险 / 待办：ARMCC 特有警告/错误未验证。
- 关联记录：1、2、3

### [7] 复制字库源码并适配 F2_CS 与硬件 SPI 兼容开关
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：依赖变更
- 触发来源：用户需求「这是一个flash的字库，源文件使用的是硬件的SPI可是现在使用的SPI使用的是软件的引脚，希望兼容原来的硬件的代码……字库使用的是F2_CS的引脚」
- 问题现象 / 需求描述：当前工程无字库读取代码；参考工程字库 CS 为 PD5，且 SPI 收发函数直接绑定硬件 `hspi1`，与本工程 F2_CS(PA15) 及软件 SPI 不匹配。
- 根本原因：`Fontlib.h` 中 `FONT_CS_0/1` 指向 `CS_GPIO_Port/CS_Pin(PD5)`；`Fontlib.c` 内直接定义 `SPI_ReadByte/SPI_WriteByte` 调用 `HAL_SPI_Receive(&hspi1,...)`。
- 改动位置：`Core/Src/Fontlib.c` 第 13–30 行；`Core/Inc/Fontlib.h` 第 16–29 行；新增 `Core/Inc/word_stock.h`（36 KB 字模常量表）
- 改动内容：
  - 修改前：`#define FONT_CS_0 HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET)`
  - 修改后：`#define FONT_CS_0 FLASH_SPI_SelectF2()` / `#define FONT_CS_1 FLASH_SPI_Deselect()`
  - 修改前：`uint8_t SPI_ReadByte(void){ HAL_SPI_Receive(&hspi1,...); }` unconditional 定义
  - 修改后：包入 `#if (FONT_USE_HW_SPI == 1)`，默认由 `flash_spi.c` 提供同名同签名实现
- 影响范围：`http_server.c` 调用 `Read_One_Font/Read_One_Char`；`main.c` 调用 `Read_One_Font` 预热。
- 验证方式：`gcc -fsyntax-only` 通过；未上板验证。
- 遗留风险 / 待办：字库芯片型号未从源码证实（仅知使用标准 `0x03` 读命令 + 24 位地址）；GB2312 区位码到地址的映射沿用原工程，需上板确认。
- 关联记录：8、10、12

### [8] 新增两片 Flash 共用的软件 SPI 底层
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「两个FLASH使用的是同一套底层的SPI」
- 问题现象 / 需求描述：字库 Flash 与普通 Flash 共用 PC13(MOSI)/PC14(MISO)/PC15(SCK)，需要一套底层并互斥片选，避免两片同时驱动 MISO。
- 根本原因：原工程无共用 SPI 抽象；两片 Flash 的 CS 若同时有效会造成总线冲突。
- 改动位置：新增 `Core/Inc/flash_spi.h`、`Core/Src/flash_spi.c`
- 改动内容：
  - 新增 `FLASH_SPI_Init/SelectF1/SelectF2/Deselect`：选中 F2(PA15) 前先拉高 F1(PA12)，反之亦然。
  - 新增 `SPI_ReadByte/SPI_WriteByte`：模式 0 位操作（`SCK` 上升沿采样、下降沿移位），保留原函数名与签名。
  - 新增 `FONT_USE_HW_SPI` 宏：设为 1 时屏蔽本文件两个函数，改由 `Fontlib.c` 中的原硬件实现提供。
- 影响范围：`Fontlib.c`、`http_server.c` 间接依赖；不改变 W5500 使用的 SPI1(PA5/6/7)。
- 验证方式：`gcc -fsyntax-only` 通过；未上板验证时序。
- 遗留风险 / 待办：软件 SPI 速率由 HAL GPIO 翻转开销决定，未测量是否超过字库 Flash 的 `0x03` 读命令最高频率；`FONT_USE_HW_SPI=1` 分支未验证。
- 关联记录：7、9

### [9] 修正两片 Flash 片选初始电平并显式初始化
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：实现共用 SPI 时发现 `MX_GPIO_Init()` 将 F1_CS/F2_CS 均置为低电平
- 问题现象 / 需求描述：上电后两片 Flash 同时被选中，共用 MISO 可能总线冲突。
- 根本原因：`gpio.c` 中 `HAL_GPIO_WritePin(GPIOA, RST_Pin|CS_NET_Pin|LED_Pin|F1_CS_Pin|F2_CS_Pin, GPIO_PIN_RESET)` 把两个 CS 一起拉低。
- 改动位置：`Core/Src/gpio.c` 第 55–58 行；`Core/Src/main.c` `USER CODE BEGIN 2`
- 改动内容：
  - 修改前：`HAL_GPIO_WritePin(GPIOA, RST_Pin|CS_NET_Pin|LED_Pin|F1_CS_Pin|F2_CS_Pin, GPIO_PIN_RESET);`
  - 修改后：拆为 `RST|CS_NET|LED` 置 `RESET`，`F1_CS|F2_CS` 置 `SET`。
  - `main.c` 增加 `#include "flash_spi.h"` 与 `FLASH_SPI_Init();`
- 影响范围：字库 Flash、普通 Flash 的片选状态；不影响 W5500 的 CS_NET。
- 验证方式：`gcc -fsyntax-only` 通过；未上板测量 CS 电平。
- 遗留风险 / 待办：无
- 关联记录：8

### [10] HTTP 服务新增字模接口与网页黑白方格渲染
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「通过网页输入单个汉字，在网页中显示使用方格的黑和白显示出字形」
- 问题现象 / 需求描述：原测试页只显示状态，无汉字输入与字形显示能力。
- 根本原因：HTTP 服务无字模路由，页面无输入与渲染逻辑；浏览器输入为 UTF-8，而字库按 GB2312 区位码寻址。
- 改动位置：`Core/Src/http_server.c`（路由与页面、辅助函数 109 行、请求解析）
- 改动内容：
  - 新增路由 `GET /api/font?code=XXXX&size=N`：校验 GBK 码范围后调用 `Read_One_Font`（12x12/15x16/24x24/32x32）或 `Read_One_Char`（ASCII 8x16），返回 `{"code":...,"w":...,"h":...,"data":"十六进制点阵"}`。
  - 请求解析中分离 `?` 查询串；新增 `http_hex_digit/http_parse_code/http_font_response`。
  - 页面新增输入框、字号下拉、显示按钮；JS 用 `TextDecoder('gbk')` 穷举 0xA1A1–0xF7FE 将输入汉字转为 GBK 码，再 `fetch` 接口，按 `bytes[col*(h/8)+row/8] & (0x80>>(row%8))` 生成黑白方格。
- 影响范围：`http_server.c` 路由表；`/api/status` 与原测试页其余逻辑保留。
- 验证方式：`gcc -fsyntax-only` 通过；HTML/JS 字符串人工检查；未用浏览器实际验证渲染。
- 遗留风险 / 待办：依赖浏览器支持 `TextDecoder('gbk')`（Chrome/Edge/Firefox 支持）；点阵位序按参考工程 `DrawFontData` 的“列优先、MSB 在前”推导，若实际显示旋转 90° 需调整 JS 取位公式。
- 关联记录：7、12

### [11] Keil 工程新增 Font/Flash 源文件分组
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：新增 `.c` 文件后 Keil 未纳入编译。
- 问题现象 / 需求描述：`flash_spi.c`、`Fontlib.c` 不在工程中，无法链接。
- 根本原因：`NET_test.uvprojx` 未包含这两个文件路径。
- 改动位置：`MDK-ARM/NET_test.uvprojx`，`Groups` 末尾
- 改动内容：新增 `<Group>Font/Flash</Group>`，含 `../Core/Src/flash_spi.c` 与 `../Core/Src/Fontlib.c`；`word_stock.h`、`Fontlib.h`、`flash_spi.h` 位于已有包含路径 `../Core/Inc`。
- 影响范围：编译构建。
- 验证方式：PowerShell `[xml]` 解析通过（`XML OK`）；未在 Keil 中实际编译。
- 遗留风险 / 待办：`word_stock.h` 约 36 KB 常量，64 KB Flash 的 STM32F103C8 容量需实测确认。
- 关联记录：7、8

### [12] 增加字库首次读取预热
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：参考工程 `screen.c` 第 653–657 行注释「首次读取前空读一次，避免第一个字读出全 0xFF」
- 问题现象 / 需求描述：首次读字可能返回全 `0xFF`（空白字形）。
- 根本原因：字库 Flash 或读取状态机需要一次初始化读取。
- 改动位置：`Core/Src/main.c` `USER CODE BEGIN 2`
- 改动内容：
  - 修改前：仅 `FLASH_SPI_Init();`
  - 修改后：在其后增加 `{ uint8_t dummy_font[32]; (void)Read_One_Font(0xA1A1u, dummy_font, FONT_SIZE_15x16); }`，并增加 `#include "Fontlib.h"`。
- 影响范围：启动流程，增加一次 32 字节字库读取。
- 验证方式：`gcc -fsyntax-only` 通过；未上板验证首字是否正常。
- 遗留风险 / 待办：需上板确认首字显示。
- 关联记录：7、10

### [13] 新增工作日志文件并落盘历史记录
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：用户明确指令「对写入文件中，做好工作记录」（承接上一轮询问「你把日志文件写在哪里了？」）
- 问题现象 / 需求描述：此前 [1]–[12] 条日志仅输出在对话回复中，未落盘，无法跨轮次追溯。
- 根本原因：`AGENTS.md` 规则要求的是「本轮回复中产出工作日志条目」，未强制落盘文件；在用户明确要求前未创建文档文件。
- 改动位置：新增 `Core/WORKLOG.md`
- 改动内容：新建文件；按原模板完整写入 [1]–[12] 条记录，并保留编号、时间、类型、触发来源、问题现象/需求描述、根本原因、改动位置、改动内容、影响范围、验证方式、遗留风险/待办、关联记录全部字段；文件头声明编号连续与「记录修正」规则。
- 影响范围：仅新增日志文件；不改动任何源码或 Keil 工程文件。
- 验证方式：写入后读取文件确认内容存在；未做编译验证（纯文档）。
- 遗留风险 / 待办：文件路径为 `Core/WORKLOG.md`，若用户希望放在其他位置需再调整。
- 关联记录：1–12

### [14] 修复网页白屏：分块发送遇 SOCK_BUSY 提前退出
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户反馈「网页端没有显示连接失败，只是全部显示的一个白屏，没有显示异常，加载的圈圈一直转，ping 地址是可以通的」
- 问题现象 / 需求描述：浏览器能连上 W5500（ping 通、无连接失败），但页面全白且加载指示器持续旋转，说明 HTTP 响应未完整送达。
- 根本原因：`http_send()` 中 `send()` 返回 `SOCK_BUSY`（宏值为 0）时被 `<= 0` 判定为错误并 `break`。`socket.c` 的阻塞式 `send()` 在上一包 `Sn_IR_SENDOK` 未到时返回 `SOCK_BUSY=0`，并非真正错误；网页 `HTTP_PAGE` 超过单次 TX 缓冲需分多块发送，第一块发出后第二块即因 `SOCK_BUSY` 退出，导致只发了响应头、响应体未发出，浏览器按 `Content-Length` 一直等待剩余字节。
- 改动位置：`Core/Src/http_server.c`，`http_send()` 函数
- 改动内容：
  - 修改前：`if (send(HTTP_SOCKET, ..., chunk) <= 0) { break; } offset += chunk;`
  - 修改后：每块发送改为无限循环重试：`sent = send(...)`；`sent > 0` 则提交本块并 `offset += chunk`；`sent < 0` 视为真正错误直接返回；`sent == 0`（`SOCK_BUSY`）时等待 `Sn_IR_SENDOK` 后重试，累计 2000 ms 超时则返回。
- 影响范围：`http_send()` 的所有调用方（`http_respond()` 的响应头与响应体发送）；不改变路由、协议与字模接口。
- 验证方式：`gcc -fsyntax-only` 通过（未上板）；预期重新编译下载后网页可完整显示。
- 遗留风险 / 待办：未在真实硬件与浏览器中验证；若仍白屏，需进一步排查 `http_respond()` 末尾等待 `Sn_IR_SENDOK` 的逻辑或 TX 缓冲 2KB 的分块大小。
- 关联记录：3、10

### [15] 字库请求失败增加前端诊断信息
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户反馈「现在输入文字显示请求失败？是不是软件SPI引脚的初始化有问题」，并要求检查软件 SPI 引脚初始化
- 问题现象 / 需求描述：网页输入汉字后显示「请求失败」，无法判断是 HTTP 状态码、非 JSON 响应还是 SPI 读取问题。
- 根本原因：`http_server.c` 的 `HTTP_PAGE` 中 fetch 的 `.catch()` 固定输出「请求失败」，且不检查 `r.ok`、不解析非 JSON 响应，缺少定位依据。经检查 `main.h`/`gpio.c`/`stm32f1xx_hal_msp.c`，软件 SPI 引脚（PC13/PC14/PC15、F1_CS=PA12、F2_CS=PA15）初始化正确，`__HAL_AFIO_REMAP_SWJ_NOJTAG()` 已释放 PA15，排除 JTAG 占用导致片选失效。
- 改动位置：`Core/Src/http_server.c`，`HTTP_PAGE` 字符串中的 fetch 处理
- 改动内容：
  - 修改前：`.then(function(r){return r.json();}).then(function(d){...}).catch(function(){msg.textContent='请求失败';});`
  - 修改后：`.then(function(r){if(!r.ok)throw new Error('HTTP '+r.status);return r.text();}).then(function(t){var d;try{d=JSON.parse(t);}catch(e){msg.textContent='JSON错误: '+t.slice(0,60);return;}...}).catch(function(e){msg.textContent='请求失败: '+(e&&e.message?e.message:e);});`
- 影响范围：仅网页前端 JavaScript 的错误提示；不改变 `/api/font` 服务端逻辑、SPI 初始化或字模接口。
- 验证方式：`gcc -fsyntax-only` 通过（未上板）；预期重新编译下载后前端显示 HTTP 状态码或 JSON 错误片段。
- 遗留风险 / 待办：需用户访问 `http://192.168.97.138/api/font?code=D6D0&size=1` 并反馈返回内容，以确认是服务端响应问题还是前端渲染问题；SPI 读字库是否正确尚未在硬件验证。
- 关联记录：10、14

### [16] 修复字库 JSON 缺失闭合大括号导致前端解析失败
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户反馈 API 返回内容 `{"code":"D6D0","w":16,"h":16,"data":"01000100010001003FF8210821082108210821083FF821080100010001000100"`（结尾无 `}`）
- 问题现象 / 需求描述：网页输入汉字显示「请求失败」，但浏览器直接访问 `/api/font` 能拿到数据，说明服务端与软件 SPI 正常，问题在 JSON 格式本身。
- 根本原因：`http_font_response()` 构造 JSON 时，`snprintf` 写入前缀 `{"code":...,"data":"`，循环追加字模十六进制后，只追加了结尾双引号 `'\"'`，遗漏了闭合大括号 `'}'`，导致返回的是非法 JSON，`JSON.parse()` 抛异常进入 `.catch()`。
- 改动位置：`Core/Src/http_server.c`，`http_font_response()` 函数末尾（约第 282 行）
- 改动内容：
  - 修改前：`if (off < (sizeof(body) - 1u)) body[off++] = '\"'; body[off] = '\0';`
  - 修改后：`if ((off + 2u) < sizeof(body)) { body[off++] = '\"'; body[off++] = '}'; } body[off] = '\0';`
- 影响范围：`/api/font` 的 JSON 响应体；字模数据内容、前端渲染逻辑、SPI 读取均不变。
- 验证方式：`gcc -fsyntax-only` 通过（未上板）；用户返回的 `"data"` 内容经人工核对为"中"字列优先点阵（16列×2字节，每字节 MSB 在前），证明 `Read_One_Font` 与软件 SPI 工作正常；预期补齐 `}` 后前端可正常显示字形。
- 遗留风险 / 待办：未在真实硬件与浏览器验证最终渲染；若字形方向异常，再按记录 10 的取位公式调整。
- 关联记录：10、15

### [17] 修复汉字字形转置（行/列优先取位颠倒）
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户上传浏览器截图（输入"港"字、24x24）并反馈「现在字体的方向错误」
- 问题现象 / 需求描述：网页显示的汉字点阵沿对角线转置（横竖颠倒），字形不可辨认。
- 根本原因：前端 `HTTP_PAGE` 的 JS 按**列优先**取位 `bytes[c*bpc + floor(r/8)]` 与 `0x80>>(r%8)`，但字库返回的点阵实为**行优先**（每行 `ceil(w/8)` 字节，字节 MSB 对应最左列）。以记录 16 中"中"字数据复核：按行优先解读，行4=`0x3FF8` 为"口"顶边、行5–9=`0x2108` 为左右边、中间列竖线贯穿，正好构成"中"字；按列优先解读则得到转置图，与截图一致。
- 改动位置：`Core/Src/http_server.c`，`HTTP_PAGE` 字符串中的点阵渲染循环（约第 88–92 行）
- 改动内容：
  - 修改前：`var bpc=Math.ceil(d.h/8); ... var b=bytes[c*bpc+Math.floor(r/8)]; ... (b&(0x80>>(r%8)))`
  - 修改后：`var bpr=Math.ceil(d.w/8); ... var b=bytes[r*bpr+Math.floor(c/8)]; ... (b&(0x80>>(c%8)))`
- 影响范围：仅网页前端点阵取位；服务端字模数据、`/api/font` 接口、软件 SPI 读取均不变。
- 验证方式：`gcc -fsyntax-only` 通过（未上板）；以"中"字数据手工推演行优先取位可还原正确字形，作为逻辑依据。
- 遗留风险 / 待办：未在真实浏览器验证；若仍方向异常（如镜像/180°旋转），需再按截图调整取位公式。
- 关联记录：10、16、18

### [18] 记录修正：更正记录 10/16 中"列优先"的误判
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：记录修正
- 触发来源：记录 17 定位字形转置时发现此前对字模位序的判断有误
- 问题现象 / 需求描述：记录 10 与记录 16 中称字模为"列优先、MSB 在前"并据此写前端取位公式，该结论错误，导致字形转置缺陷（记录 17）。
- 根本原因：早期仅依据参考工程 `DrawFontData` 的变量命名推断位序，未用实际字模数据验证；记录 16 虽拿到"中"字数据，但仍误判为列优先。
- 改动位置：`Core/WORKLOG.md`（本记录本身，为新增条目，不覆盖历史条目）；对应代码修正见记录 17
- 改动内容：声明记录 10 的取位公式 `bytes[col*(h/8)+row/8]` 与记录 16 的"列优先点阵"判断均作废；正确位序为**行优先**：每行 `ceil(w/8)` 字节，字节 MSB 对应最左列，前端应按 `bytes[r*ceil(w/8)+floor(c/8)] & (0x80>>(c%8))` 取位。
- 影响范围：仅工作日志的结论更正；不改变任何历史条目内容，也不改变已落地的代码（代码以记录 17 为准）。
- 验证方式：以"中"字 32 字节数据手工推演，行优先取位还原出正确"中"字。
- 遗留风险 / 待办：无
- 关联记录：10、16、17

### [19] 新增 .gitignore 用于 GitHub 基础工程
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：用户需求「我现在想把这个测试的工程保存在github，你现在在这个工程生成一个基础工程，你取一个名字，做一个简洁，后面还会有变种的新功能」
- 问题现象 / 需求描述：当前 Keil 工程目录中混有 109 个编译产物（`.o/.crf/.d/.map/.hex` 等）与 J-Link 本地配置，若直接提交 GitHub 会纳入大量无需版本控制的二进制文件。
- 根本原因：工程此前未做 Git 版本控制准备，无忽略规则。
- 改动位置：新增 `/.gitignore`（工程根目录）
- 改动内容：新建文件，排除 Keil 编译产物（`*.o/*.d/*.crf/*.axf/*.hex/*.map/*.htm/*.lnp/*.sct/*.dep/*.lst/*.bak/*.tmp/*.log`、`MDK-ARM/NET_test/`、`MDK-ARM/DebugConfig/`）、J-Link 本地配置、Windows 与编辑器临时文件；保留 `*.uvprojx/*.uvoptx/*.ioc/.mxproject/RTE_Components.h/startup*.s` 等工程与源码文件。
- 影响范围：Git 版本控制行为；不影响编译与运行。
- 验证方式：`git ls-files --others --exclude-standard` 统计 940 个待提交文件（Core 35 + Drivers 897 + MDK-ARM 4 + 根目录 4），`git status --ignored --short` 确认 5 类产物目录/文件被忽略。
- 遗留风险 / 待办：未提交到 GitHub（未执行 `git commit`/`git push`，等待用户提供仓库地址）。
- 关联记录：20、21

### [20] 新增 README.md 作为基础工程说明
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：同记录 19（用户要求「取一个名字，做一个简洁」的基础工程）
- 问题现象 / 需求描述：工程缺少入口文档，后续变种使用者无法快速了解功能、引脚、编译与派生方式。
- 根本原因：原工程为测试现场逐步搭建，未编写说明文档。
- 改动位置：新增 `/README.md`（工程根目录）
- 改动内容：新建文件，取工程名 `STM32F103-W5500-WebFont`；包含功能概述、目录结构、硬件引脚表、编译与运行方法、网络参数位置、HTTP 接口路由、派生变种建议（独立模块文件、WORKLOG 新编号、Git 分支管理 base）。
- 影响范围：文档；不影响编译与运行。
- 验证方式：文件已写入并可由 `git status` 识别为待提交文件。
- 遗留风险 / 待办：工程名仅为文档命名，`NET_test.uvprojx` / `NET_test.ioc` 未改名（改名需同步 Keil 与 CubeMX 内部引用，风险高，本轮保持简洁未做）。
- 关联记录：19、21

### [21] 在工程根目录初始化 Git 仓库
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：同记录 19（用户要求「保存在 github」）
- 问题现象 / 需求描述：工程目录无 `.git`，无法进行版本控制与推送 GitHub。
- 根本原因：此前未初始化仓库。
- 改动位置：新增 `/.git/`（工程根目录，由 `git init` 生成）
- 改动内容：执行 `git init -q`，生成 Git 元数据目录；未配置 remote、未提交 commit。
- 影响范围：仅新增版本控制元数据；不改动任何源码或工程文件。
- 验证方式：`git status --short` 正常列出待提交文件；`git ls-files --others --exclude-standard` 统计 940 个文件。
- 遗留风险 / 待办：需用户提供 GitHub 仓库地址后执行 `git remote add origin <url>`、`git add .`、`git commit`、`git push -u origin main`；未执行提交。
- 关联记录：19、20

### [22] 配置 GitHub 远端并暂存全部工程文件
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：用户提供仓库地址「https://github.com/HiBO-074300/STM32F103-W5500-WebFont，你自己添加」
- 问题现象 / 需求描述：本地仓库已初始化但无远端，工程文件未暂存，无法推送 GitHub。
- 根本原因：记录 21 仅执行 `git init`，等待用户提供仓库地址。
- 改动位置：`.git/config`（新增 remote）、Git 暂存区
- 改动内容：执行 `git remote add origin https://github.com/HiBO-074300/STM32F103-W5500-WebFont.git`；执行 `git add .` 暂存 940 个文件。
- 影响范围：Git 元数据与暂存区；不改动源码内容。
- 验证方式：`git remote -v` 显示 origin 的 fetch/push 地址正确；`git diff --cached --name-only` 统计 940 个文件；6 个 ioLibrary 文件出现 LF→CRLF 警告（仅换行符提示，不影响内容）。
- 遗留风险 / 待办：无
- 关联记录：19、23

### [23] 创建基础版本初始提交
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：同记录 22（用户授权「你自己添加」）
- 问题现象 / 需求描述：暂存的 940 个文件尚未形成提交。
- 根本原因：新仓库无 commit。
- 改动位置：Git 提交历史
- 改动内容：执行 `git commit -m 'base: STM32F103-W5500-WebFont initial project'`，生成提交 `6f1707d`。
- 影响范围：Git 提交历史；不改动工作区文件。
- 验证方式：`git --no-pager log -1 --format='%h %s'` 显示 `6f1707d base: STM32F103-W5500-WebFont initial project`；`git status --short` 为空。
- 遗留风险 / 待办：无
- 关联记录：22、24

### [24] 推送 master 分支到 GitHub 并建立跟踪
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：同记录 22
- 问题现象 / 需求描述：本地提交未推送到远端，GitHub 仓库为空。
- 根本原因：未执行 push。
- 改动位置：GitHub 远端仓库 `HiBO-074300/STM32F103-W5500-WebFont` 的 `master` 分支
- 改动内容：执行 `git push -u origin master`，推送提交 `6f1707d`，建立 `master` 跟踪 `origin/master`。
- 影响范围：GitHub 远端仓库；不改变本地源码。
- 验证方式：`git push` 输出 `[new branch] master -> master` 与 `branch 'master' set up to track 'origin/master'`；`git rev-parse HEAD` 与 `git rev-parse origin/master` 均为 `6f1707d1ce9e2cc96fd0e3fb84b1b5136707c164`；`git status --short` 为空。PowerShell 将 git 进度输出（stderr）显示为 `RemoteException`，但退出码 0 且远端 HEAD 一致，推送成功。
- 遗留风险 / 待办：GitHub 仓库默认分支可能为 `main`，本工程推送分支为 `master`；如需以 `main` 为默认分支，需在 GitHub 网页端调整或本地重命名分支后重推。
- 关联记录：22、23

### [25] 补提交工作日志 22-24 使本地与远端同步
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：记录 22-24 写入 WORKLOG.md 发生在初始提交 `6f1707d` 之后，导致本地工作区比远端多出未提交改动
- 问题现象 / 需求描述：初始提交与推送完成后才写入记录 22-24，远端缺少这部分日志，本地工作区不干净。
- 根本原因：日志写入与 Git 提交推送存在先后顺序，非同一原子操作。
- 改动位置：`Core/WORKLOG.md`（记录 25 本身）；Git 提交历史与远端
- 改动内容：执行 `git add Core/WORKLOG.md`、`git commit -m 'docs: append worklog records 22-24'`、`git push origin master`，生成提交 `9fa0242`。
- 影响范围：仅文档同步与 Git 历史；源码不变。
- 验证方式：`git log` 显示 `9fa0242 docs: append worklog records 22-24`；`git rev-parse HEAD` 与 `git rev-parse origin/master` 均为 `9fa02424a6fddfece9389fb78b921e27d630c0e9`；`git status --short` 为空。
- 遗留风险 / 待办：无（记录 25 本身随下一次日志同步提交推送，形成收尾；避免无限递归，本条为最后一次日志同步记录）。
- 关联记录：23、24

### [26] 将基础工程本体移入 base/ 目录分层
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：重构优化
- 触发来源：用户「我觉得应该是不是在建一层目录」并确认目录名「base/」
- 问题现象 / 需求描述：仓库根目录直接摊开 Core/、Drivers/、MDK-ARM/ 等，后续变种功能会使顶层混乱，需将基础工程本体与仓库级内容/未来变种分离。
- 根本原因：初版 GitHub 基础工程采用扁平结构，未预留变种分层。
- 改动位置：`base/`（新建），原 `Core/`、`Drivers/`、`MDK-ARM/`、`NET_test.ioc`、`.mxproject` 移入其中；仓库根目录保留 `README.md`、`.gitignore`、`.git/`；新增 `variants/` 预留目录
- 改动内容：
  - 用 `git mv` 移动 `Drivers/`、`.mxproject`、`NET_test.ioc`；`Core/`、`MDK-ARM/` 因 IDE 工作区占用导致整目录 `git mv` 失败（Permission denied），改用临时脚本逐文件 `git mv`（35 个 Core 文件、7 个 MDK 文件）后删除脚本与空目录。
  - 更新 `.gitignore`：`MDK-ARM/...` → `base/MDK-ARM/...`。
  - 更新 `README.md`：目录结构改为 `base/` 分层，编译/源码路径加 `base/` 前缀，派生说明改为在 `variants/` 下创建变种。
  - 创建 `variants/` 预留目录（空目录，Git 不跟踪，需放 .gitkeep 才可提交）。
- 影响范围：仓库目录结构；Keil 工程内引用均为相对路径 `../Core/...`、`../Drivers/...`，移动后相对关系不变，已验证所有引用路径存在；**IDE 工作区路径 `.../NET_test/Core` 失效，需重新打开到 `.../NET_test/base/Core`**。
- 验证方式：`git status --short` 显示 938 个重命名；`base/Core` 35 文件、`base/MDK-ARM` 7 文件；`search_content` 确认 `.uvprojx` 的 IncludePath 与 FilePath 均为 `../Core/`、`../Drivers/` 且目标目录存在；`gcc -fsyntax-only` 未重跑（源码内容未变）。
- 遗留风险 / 待办：根目录残留空目录 `Core/`、`MDK-ARM/`（IDE 占用无法删除，空目录不被 Git 跟踪，不影响仓库）；`variants/` 为空未纳入版本控制；需在 Keil/CodeBuddy 重新打开工作区到 `base/Core`/`base/MDK-ARM` 验证编译。
- 关联记录：21、27

---

## 变更汇总表

| 序号 | 类型 | 一句话摘要 | 文件 | 状态 |
|---|---|---|---|---|
| 1 | 依赖变更 | 复制 WIZnet ioLibrary W5500 驱动 | `Core/ioLibrary/Ethernet/*` | 已完成 |
| 2 | 功能新增 | W5500 移植层，静态 IP 192.168.97.138 | `Core/Inc/w5500_port.h`, `Core/Src/w5500_port.c` | 已完成 |
| 3 | 功能新增 | HTTP 测试页服务（80 端口） | `Core/Inc/http_server.h`, `Core/Src/http_server.c` | 已完成 |
| 4 | 配置调整 | main 接入初始化与轮询 | `Core/Src/main.c` | 已完成 |
| 5 | 配置调整 | INT 改 EXTI0 下降沿并加 ISR | `Core/Src/gpio.c`, `Core/Src/stm32f1xx_it.c` | 已完成 |
| 6 | 配置调整 | Keil 工程加分组与头文件路径 | `MDK-ARM/NET_test.uvprojx` | 已完成 |
| 7 | 依赖变更 | 复制字库源码，CS 改 F2_CS，保留硬件 SPI 兼容开关 | `Core/Src/Fontlib.c`, `Core/Inc/Fontlib.h`, `Core/Inc/word_stock.h` | 已完成 |
| 8 | 功能新增 | 两片 Flash 共用的软件 SPI 底层与互斥片选 | `Core/Inc/flash_spi.h`, `Core/Src/flash_spi.c` | 已完成 |
| 9 | 缺陷修复 | 上电时释放两片 Flash 片选 | `Core/Src/gpio.c`, `Core/Src/main.c` | 已完成 |
| 10 | 功能新增 | 汉字输入、字模 JSON 接口与黑白方格渲染 | `Core/Src/http_server.c` | 已完成 |
| 11 | 配置调整 | Keil 新增 Font/Flash 分组 | `MDK-ARM/NET_test.uvprojx` | 已完成 |
| 12 | 缺陷修复 | 字库首次读取预热 | `Core/Src/main.c` | 已完成 |
| 13 | 配置调整 | 新增工作日志文件并落盘历史记录 | `Core/WORKLOG.md` | 已完成 |
| 14 | 缺陷修复 | 修复分块发送遇 SOCK_BUSY 提前退出导致网页白屏 | `Core/Src/http_server.c` | 已完成 |
| 15 | 缺陷修复 | 字库请求失败增加前端诊断信息 | `Core/Src/http_server.c` | 已完成 |
| 16 | 缺陷修复 | 修复字库 JSON 缺失闭合大括号导致前端解析失败 | `Core/Src/http_server.c` | 已完成 |
| 17 | 缺陷修复 | 修复汉字字形转置（行/列优先取位颠倒） | `Core/Src/http_server.c` | 已完成 |
| 18 | 记录修正 | 更正记录 10/16 中"列优先"的误判 | `Core/WORKLOG.md` | 已完成 |
| 19 | 配置调整 | 新增 .gitignore 排除 Keil 编译产物 | `.gitignore` | 已完成 |
| 20 | 配置调整 | 新增 README.md 作为基础工程说明 | `README.md` | 已完成 |
| 21 | 配置调整 | 工程根目录初始化 Git 仓库 | `.git/` | 已完成 |
| 22 | 配置调整 | 配置 GitHub 远端并暂存全部工程文件 | `.git/config`、暂存区 | 已完成 |
| 23 | 配置调整 | 创建基础版本初始提交 `6f1707d` | Git 提交历史 | 已完成 |
| 24 | 配置调整 | 推送 master 分支到 GitHub 并建立跟踪 | GitHub 远端仓库 | 已完成 |
| 25 | 配置调整 | 补提交工作日志 22-24 使本地与远端同步 | `Core/WORKLOG.md`、Git 提交历史 | 已完成 |
| 26 | 重构优化 | 将基础工程本体移入 base/ 目录分层 | `base/`、`.gitignore`、`README.md` | 已完成 |

## 当前累计改动清单

| 序号 | 类型 | 一句话摘要 | 文件 | 状态 |
|---|---|---|---|---|
| 1 | 依赖变更 | 复制 WIZnet ioLibrary W5500 驱动 | `Core/ioLibrary/Ethernet/*` | 已完成 |
| 2 | 功能新增 | W5500 移植层，静态 IP 192.168.97.138 | `Core/Inc/w5500_port.h`, `Core/Src/w5500_port.c` | 已完成 |
| 3 | 功能新增 | HTTP 测试页服务（80 端口） | `Core/Inc/http_server.h`, `Core/Src/http_server.c` | 已完成 |
| 4 | 配置调整 | main 接入初始化与轮询 | `Core/Src/main.c` | 已完成 |
| 5 | 配置调整 | INT 改 EXTI0 下降沿并加 ISR | `Core/Src/gpio.c`, `Core/Src/stm32f1xx_it.c` | 已完成 |
| 6 | 配置调整 | Keil 工程加分组与头文件路径 | `MDK-ARM/NET_test.uvprojx` | 已完成 |
| 7 | 依赖变更 | 复制字库源码，CS 改 F2_CS，保留硬件 SPI 兼容开关 | `Core/Src/Fontlib.c`, `Core/Inc/Fontlib.h`, `Core/Inc/word_stock.h` | 已完成 |
| 8 | 功能新增 | 两片 Flash 共用的软件 SPI 底层与互斥片选 | `Core/Inc/flash_spi.h`, `Core/Src/flash_spi.c` | 已完成 |
| 9 | 缺陷修复 | 上电时释放两片 Flash 片选 | `Core/Src/gpio.c`, `Core/Src/main.c` | 已完成 |
| 10 | 功能新增 | 汉字输入、字模 JSON 接口与黑白方格渲染 | `Core/Src/http_server.c` | 已完成 |
| 11 | 配置调整 | Keil 新增 Font/Flash 分组 | `MDK-ARM/NET_test.uvprojx` | 已完成 |
| 12 | 缺陷修复 | 字库首次读取预热 | `Core/Src/main.c` | 已完成 |
| 13 | 配置调整 | 新增工作日志文件并落盘历史记录 | `Core/WORKLOG.md` | 已完成 |
| 14 | 缺陷修复 | 修复分块发送遇 SOCK_BUSY 提前退出导致网页白屏 | `Core/Src/http_server.c` | 已完成 |
| 15 | 缺陷修复 | 字库请求失败增加前端诊断信息 | `Core/Src/http_server.c` | 已完成 |
| 16 | 缺陷修复 | 修复字库 JSON 缺失闭合大括号导致前端解析失败 | `Core/Src/http_server.c` | 已完成 |
| 17 | 缺陷修复 | 修复汉字字形转置（行/列优先取位颠倒） | `Core/Src/http_server.c` | 已完成 |
| 18 | 记录修正 | 更正记录 10/16 中"列优先"的误判 | `Core/WORKLOG.md` | 已完成 |
| 19 | 配置调整 | 新增 .gitignore 排除 Keil 编译产物 | `.gitignore` | 已完成 |
| 20 | 配置调整 | 新增 README.md 作为基础工程说明 | `README.md` | 已完成 |
| 21 | 配置调整 | 工程根目录初始化 Git 仓库 | `.git/` | 已完成 |
| 22 | 配置调整 | 配置 GitHub 远端并暂存全部工程文件 | `.git/config`、暂存区 | 已完成 |
| 23 | 配置调整 | 创建基础版本初始提交 `6f1707d` | Git 提交历史 | 已完成 |
| 24 | 配置调整 | 推送 master 分支到 GitHub 并建立跟踪 | GitHub 远端仓库 | 已完成 |
| 25 | 配置调整 | 补提交工作日志 22-24 使本地与远端同步 | `Core/WORKLOG.md`、Git 提交历史 | 已完成 |
| 26 | 重构优化 | 将基础工程本体移入 base/ 目录分层 | `base/`、`.gitignore`、`README.md` | 已完成 |
