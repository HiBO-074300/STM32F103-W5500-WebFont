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

### [27] 提交并推送 base/ 目录重构到 GitHub
- 时间：2026-11-19 HH:mm（精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：记录 26 的目录重构需同步到远端
- 问题现象 / 需求描述：base/ 重构与记录 26 写入后，本地有未提交改动，远端仍为旧结构
- 根本原因：重构操作与提交推送分属不同步骤
- 改动位置：Git 提交历史与 GitHub 远端仓库
- 改动内容：执行 git add -A、git commit -m 'refactor: move base project into base/ directory'、git push origin master，生成提交 6aa1ad7
- 影响范围：仅 Git 历史与远端目录结构；源码内容不变
- 验证方式：git log 显示 6aa1ad7；本地与 origin/master 均为 6aa1ad729876ebd70daeb9988015a28109f2df14；git status 为空
- 遗留风险 / 待办：无
- 关联记录：26

### [28] 提交并推送 find_dev 开发版本备份到 GitHub
- 时间：2026-11-23 17:06
- 类型：配置调整
- 触发来源：用户「你现在帮我把工程备份一个版本到github之中。」
- 问题现象 / 需求描述：当前开发副本 `find_dev/` 尚未纳入版本控制，仓库中同时存在 `base/` 修改与根目录 `MDK-ARM/` 删除等未提交改动，需要将当前工程状态整体备份到 GitHub。
- 根本原因：上一轮重构提交 `6aa1ad7` 之后，`find_dev/` 作为新的开发副本被创建但未加入 Git，相关目录调整也未提交。
- 改动位置：`find_dev/Core/WORKLOG.md`（追加本条记录）、`.gitignore`；Git 暂存区、提交历史；GitHub 远端 `origin/master`
- 改动内容：
  - 在 `find_dev/Core/WORKLOG.md` 追加记录 28 及两份汇总表行。
  - `.gitignore` 增加 `find_dev/MDK-ARM/NET_test/`、`DebugConfig/`、`JLinkLog.txt`、`JLinkSettings.ini` 忽略规则，与 `base/` 保持一致，避免调试产物入库。
  - `git add -A` 纳入未跟踪的 `find_dev/` 及既有未提交改动（含 `base/` 修改、根目录 `MDK-ARM/` 删除与新增）。
  - `git commit` 生成备份版本提交，`git push origin master` 推送到 GitHub。
- 影响范围：Git 历史、GitHub 远端仓库；仓库顶层新增 `find_dev/` 目录；`.gitignore` 忽略规则扩展；不影响源码逻辑与 Keil 编译配置。
- 验证方式：`git log --oneline -3` 查看新提交；`git status --short` 应为空；`git rev-parse HEAD` 与 `git rev-parse origin/master` 一致。
- 遗留风险 / 待办：Keil 编译产物依据 `.gitignore` 全局规则排除，但 `.uvguix.HiBO` 为 Keil 用户界面配置，随备份纳入版本控制；未做硬件编译验证。
- 关联记录：27

### [29] 新增 UDP 设备发现上位机（局域网设备列表显示）
- 时间：2026-11-23 17:18
- 类型：功能新增
- 触发来源：用户「在上位机，发现设备的之后在列表中显示出局域网中的所有设备的列表。」
- 问题现象 / 需求描述：上位机需通过 UDP 广播发现局域网内所有设备，并在界面列表中集中显示各设备的名称、标识、IP、端口、工作模式、固件版本与状态；多设备时需靠识别信息避免混淆。
- 根本原因：工程此前只有嵌入式侧工程，无任何上位机代码；设备发现与列表展示功能缺失。
- 改动位置：`find_dev/host/device_finder.py`（新建）；`find_dev/Core/WORKLOG.md`（追加本条记录及汇总表行）
- 改动内容：
  - 新建 `find_dev/host/device_finder.py`：基于 Python3 标准库 tkinter 实现 GUI，无需第三方依赖。
  - 定义协议：26 字节头（`>4sBBI6s6sHH`）+ 2 字节累加和 + payload；`DISCOVER=1`、`REPLY=2`；`target/src` 为 6 字节 MAC（`FF*6` 为广播），`session` 用于请求/应答配对；`REPLY` payload 为 `>32s4sHBHB`。
  - `build_frame/parse_frame` 完成组包、magic+version 校验、长度校验与校验和校验。
  - UDP socket 绑定临时端口并开启 `SO_BROADCAST`，启动时及每 3 秒（可切换）向 `255.255.255.255:50000` 广播 `DISCOVER`；接收线程解析 `REPLY` 后经 `queue` 送主线程，按 `src` MAC 去重更新字典并刷新 Treeview 列表。
  - 状态栏显示设备数量与更新时间；超过 10 秒未响应的设备自动移除；提供「发现设备」「清空」操作与端口输入。
- 影响范围：新增独立上位机脚本，不影响嵌入式固件、`base/` 与 Keil 工程；仅依赖 Python 3.11 与 tkinter。
- 验证方式：`python --version` 为 3.11.5；`python -m py_compile find_dev/host/device_finder.py` 输出 `SYNTAX_OK`；未启动 GUI 与真实设备联调（当前无配套设备端 UDP 发现应答实现）。
- 遗留风险 / 待办：设备端尚未实现 `DISCOVER`/`REPLY` 协议，当前列表在无应答时为空；下一轮需实现 W5500 侧 UDP 发现应答与参数配置命令处理，并做端到端联调；`python -m py_compile` 在 `find_dev/host/__pycache__/` 生成的 `.pyc` 缓存清理命令未获审批通过，提交前需手动删除或在 `.gitignore` 中排除 `__pycache__/`、`*.pyc`。
- 关联记录：无

### [30] 固件侧实现 UDP 发现监听与 REPLY 应答
- 时间：2026-11-24 10:30
- 类型：功能新增
- 触发来源：用户「需要在单片机程序去回应这个程序。」
- 问题现象 / 需求描述：记录 29 已完成上位机广播发现，但设备端没有监听与应答，上位机列表恒为空；需让单片机监听 UDP 发现广播并回 REPLY，与上位机打通。
- 根本原因：固件此前只有 socket0 的 TCP HTTP 服务（记录 2/3），未使用 W5500 其余 socket 建立 UDP 监听，缺少发现协议设备端实现。
- 改动位置：`find_dev/Core/Inc/discovery.h`（新建）、`find_dev/Core/Src/discovery.c`（新建）、`find_dev/Core/Src/main.c`（接入初始化与轮询）、`find_dev/MDK-ARM/NET_test.uvprojx`（新增源文件条目）、`find_dev/Core/WORKLOG.md`（本条记录及汇总表行）
- 改动内容：
  - 新增 `discovery.c/.h`：使用 socket1、`Sn_MR_UDP`、端口 50000；`discovery_poll()` 状态机在 `SOCK_CLOSED` 时 `socket()` 打开、`SOCK_UDP` 时按 `getSn_RX_RSR()` 取数据、`recvfrom()` 接收 5 参数版本，解析校验后 `sendto()` 单播应答到上位机源 IP/端口。
  - 协议与上位机一致（大端）：`DVID` magic + 版本 1 + 类型 + session[4] + target[6] + src[6] + seq[2] + plen[2] + 累加和 2 字节；DISCOVER(1) 请求，REPLY(2) 应答 payload 42 字节含设备名/IP/端口/mode/fw/status。
  - 校验：magic、版本、类型、长度（28+plen）、target（广播 FF*6 或本机 MAC）、累加和；session/seq 原样回传，target 填请求方标识，src 填本机 MAC `NET_MAC_ADDR`。
  - `main.c`：USER CODE Includes 加 `discovery.h`；初始化段在 `http_server_init()` 后加 `discovery_init()`；while 循环在 `http_server_poll()` 后加 `discovery_poll()`。
  - Keil `NET_test.uvprojx`：在 `Application/User/Core` 分组增加 `discovery.c`（`../Core/Src/discovery.c`）。
- 影响范围：新增 socket1 UDP 监听，与 HTTP socket0 并存；不改动网络参数、HTTP 逻辑与字库模块；需在 Keil 重新编译下载固件。
- 验证方式：Python 复现固件组包逻辑，构造 28 字节 DISCOVER，校验解析通过；生成 70 字节 REPLY 经上位机 `device_finder.parse_frame()` 解析成功，字段为 NAME=`STM32F103-W5500-WebFont`、IP=`192.168.97.138`、PORT=80、MODE=1、FW=0x0100、STATUS=0、session/seq 原样回传。自检中发现并修复 REPLY payload 少写 1 字节状态字段的缺陷（补 `DEV_STATUS_OK` 后长度 41→42）。未在 Keil/硬件编译运行验证。
- 遗留风险 / 待办：未做 Keil 编译与实板上电联调，需确认 W5500 广播包是否被正确接收（未设置 `SF_BROAD_BLOCK`，默认允许广播）；仅实现发现应答，参数配置 `SET`/`ACK` 尚未实现；`ST`/`FW` 为固定值，未接入实际配置存储。
- 关联记录：29

### [31] 新增 LED 三态诊断指示，定位广播发现链路问题
- 时间：2026-11-24 11:00
- 类型：功能新增
- 触发来源：用户连续追问「是不是因为 w5500 芯片的配置中没有开启接收广播的功能」「是不是因为单片机无法接收网络中的广播」，并确认「有一个LED可以当做显示状态」。
- 问题现象 / 需求描述：UDP 发现应答下发后上位机列表仍可能为空，无法区分是广播未到达 W5500、到达但校验失败、还是应答未发出；需用现有 LED 做可视化诊断。
- 根本原因：链路（广播到达 → 解析 → REPLY）中任一环断裂现象相同，缺少可观测手段；且 `main.c` 原用 INT 网络事件翻转同一 LED，无法反映发现协议状态。
- 改动位置：`find_dev/Core/Src/discovery.c`（LED 状态机）、`find_dev/Core/Src/main.c`（移除 INT 事件翻转 LED）、`find_dev/Core/WORKLOG.md`（本条及汇总表行）
- 改动内容：
  - `discovery.c` 增加 `main.h` 引用，新增 LED 状态机（非阻塞，`HAL_GetTick` 驱动）：心跳 1s 翻转（程序在跑）、收到任意 UDP 包快闪 100ms 持续 600ms（广播到达但校验失败）、REPLY 发送成功常亮 1s（链路全通）；事件态结束自动回心跳。
  - `discovery_poll()` 开头调用 `discovery_led_poll()`；`recvfrom` 成功后调 `discovery_led_rx()`；`sendto` 返回 >0 调 `discovery_led_reply()`。
  - 附带改动：`main.c` 主循环移除 `w5500_int_event_take()` 为真时的 `HAL_GPIO_TogglePin(LED, ...)`，改为 `(void)w5500_int_event_take()` 仅清标志，避免 INT 网络事件翻转 LED 覆盖诊断指示。
- 影响范围：仅改变 PA8 LED 的指示含义（由通用网络活动改为发现诊断）；不影响 HTTP、字库、网络参数；`w5500_int_event_take` 清除标志的副作用保留。
- 验证方式：`gcc -fsyntax-only -DSTM32F103xB`（含 HAL/CMSIS 头路径）检查 `discovery.c` 通过，EXIT=0；仅有 CMSIS `core_cm3.h` 原有 64 位主机指针 cast 警告，与本次改动无关。未做硬件编译运行验证。
- 遗留风险 / 待办：需 Keil 编译下载后实地观察 LED 三态；快闪仅表示「收到 UDP 包但校验失败」，不区分具体校验项；常亮 1s 为固定时长，不可配置。
- 关联记录：30

### [32] 修复多网卡环境下 UDP 广播走错网卡
- 时间：2026-11-24 11:30
- 类型：缺陷修复
- 触发来源：用户「python程序的广播我发现广播出去没有数据，自己都收不到」。
- 问题现象 / 需求描述：上位机广播发出后设备及手机均无反应，用户怀疑广播未真正发出。
- 根本原因：PC 存在 5 个网卡（`以太网 192.168.97.122`、`OrayBoxVpnEnt 172.16.1.247`、VMware×2、`169.254.x`），`255.255.255.255` 全局广播经默认路由从 VPN 网卡 `172.16.1.247` 发出，未走连接设备的 `192.168.97.122`，广播到不了 `192.168.97.138`。本机自测（绑定 `0.0.0.0`）确认源 IP 为 `172.16.1.247`，广播功能本身正常，是出口网卡选错。
- 改动位置：`find_dev/host/device_finder.py`（广播地址常量、socket 绑定、状态提示）；`find_dev/Core/WORKLOG.md`（本条及汇总表行）
- 改动内容：
  - 广播目标 `255.255.255.255` → 定向广播 `192.168.97.255`（设备网段），强制走正确网段。
  - 修改前：`self.sock.bind(('0.0.0.0', 0))` → 修改后：`self.sock.bind((BIND_IP, 0))`，`BIND_IP='192.168.97.122'`（设备所在网卡）；新增 `BIND_IP` 常量并提供改为 `0.0.0.0` 的注释说明。
  - 状态栏提示由 `已广播发现请求 → {ip}:{port}` → `已广播 → {ip}:{port} (网卡 {BIND_IP})`，便于确认出口网卡。
- 影响范围：仅上位机脚本；若用户网卡 IP 不是 `192.168.97.122`，需自行修改 `BIND_IP`；不影响固件与协议。
- 验证方式：`python -m py_compile` 输出 `SYNTAX_OK`；本机自测绑定 `192.168.97.122` 后 `BOUND ('192.168.97.122', 53040)`、`SEND_OK 7`，确认定向广播从正确网卡发出。未与真实设备联调。
- 遗留风险 / 待办：`BIND_IP` 硬编码为本机当前网卡 IP，换网络/换 PC 需改常数；路由器若开启 AP 隔离，定向广播仍可能不通；手机端纯监听仍收不到单播 REPLY（设计如此）。
- 关联记录：29、31

### [33] 增加通过 UDP 配置设备 IP 地址的功能
- 时间：2026-11-24 12:00
- 类型：功能新增
- 触发来源：用户「在python中增加修改IP地址的功能，」
- 问题现象 / 需求描述：上位机发现设备后，需选中设备并修改其 IP 地址；设备端需实际写入 W5500 并回确认，避免多设备配置混乱。
- 根本原因：此前协议只定义 DISCOVER/REPLY，未实现 SET/ACK 的配置下发与应答链路。
- 改动位置：`find_dev/host/device_finder.py`（SET/ACK 协议、配置 UI、选择与结果处理）；`find_dev/Core/Src/discovery.c`（SET 解析、setSIPR 写 IP、ACK 构造）；`find_dev/Core/WORKLOG.md`（本条及汇总表行）
- 改动内容：
  - 协议扩展：新增 `MSG_SET=3`、`MSG_ACK=4`；SET payload 为新 IP[4]，ACK payload 为结果码[1]（0=成功）；头部（magic/version/session/target/src/seq/校验）沿用既有格式。
  - 上位机：新增配置栏（新 IP 输入框 + 「写入 IP」按钮 + 选中目标提示）；Treeview 选中设备时自动回填其当前 IP；`set_device_ip()` 以目标设备 MAC 填 target 单播 SET；`_recv` 接收 REPLY/ACK 并按类型分派；`_on_ack()` 校验 session 与 src MAC 匹配后显示成功/失败；新增 `pending_set` 状态。
  - 固件：`s_ip` 由 const 改为可写；`discovery_parse()` 改为通用解析，返回消息类型，SET 强制 target 精确匹配本机 MAC（防误配置）且 payload 长度必须为 4；新增 `discovery_build_ack()` 与 `discovery_apply_ip()`（调用 `setSIPR` 写 W5500 并更新 s_ip）；`discovery_poll()` 按类型分派，DISCOVER 回 REPLY，SET 先回 ACK 再应用新 IP。
- 影响范围：协议新增两种消息类型，与旧 DISCOVER/REPLY 兼容；固件 REPLAY 回报当前 IP（改后为新的）；HTTP/字库不受影响；需在 Keil 重新编译下载固件。
- 验证方式：`gcc -fsyntax-only -DSTM32F103xB` 检查 `discovery.c` 通过（FW_EXIT=0，仅 CMSIS 原有指针 cast 警告）；`python -m py_compile` 通过（PY_EXIT=0）；协议自测：构造 32 字节 SET，固件侧解析条件（类型/plen/校验/target）全满足，新 IP `192.168.97.139` 正确；模拟 ACK 29 字节经上位机 `parse_frame()` 解析成功（类型4、session `0xAABBCCDD`、结果0）。未与真实设备联调。
- 遗留风险 / 待办：新 IP 未持久化到 Flash，掉电恢复编译期默认 IP；ACK 先于 `setSIPR` 发送，若写寄存器失败仍会误报成功（当前 setSIPR 无返回检查）；改 IP 后上位机 `BIND_IP`/广播地址若不在新网段需相应调整；配置仅支持 IP，掩码/网关未开放修改。
- 关联记录：29、32

### [34] 上位机重构为左右分栏界面并增加设备详情与配置项预留
- 时间：2026-11-24 12:20
- 类型：功能新增
- 触发来源：用户「我想点击之后进行对应的列表中的选项，在侧面显示具体的信息现在只有IP地址的选项，其实应该有静态和动态的IP选项。后面应该是还有其他的内容先预留出来，你也不用自己补充内容。」
- 问题现象 / 需求描述：上位机列表点击设备后无详情展示；配置项只有 IP 地址，需增加静态/动态 IP 选项，并预留其他配置位置（不填充内容）。
- 根本原因：原界面为单栏布局，选中设备仅在工具栏提示 MAC@IP，无详情面板；配置项只有 IP 输入框。
- 改动位置：`find_dev/host/device_finder.py`（仅此文件）；`find_dev/Core/WORKLOG.md`（本条及汇总表行）
- 改动内容：
  - 窗口尺寸 `920x430` → `1120x520`；布局改为左右分栏：左=设备列表 Treeview，右=详情面板（固定宽 330）。
  - 右侧详情区（`LabelFrame 设备详情`）：以 grid 展示名称/MAC/IP/端口/工作模式/固件/状态/最后响应，值只读，由 `detail_vars` 驱动。
  - 右侧配置区（`LabelFrame 参数配置`）：新增「IP 模式」下拉（静态/动态，只读选择）、新 IP 输入框、写入配置按钮；新增 ` reserve` 空白 `LabelFrame 其他配置（预留）`，不填任何内容。
  - 交互：`_on_select` 点击列表调用 `_show_detail(mac)` 回填详情与新 IP；`_render` 刷新后按 `selected_mac` 恢复选中并回填；设备离线时清空详情。
  - `set_device_ip` 改为使用 `selected_mac` 和 `devices[mac]['ip']` 作为目标；选择「动态」时提示「动态 IP(DHCP) 固件暂未实现，当前仅支持静态写入」，不改变协议。
- 影响范围：仅界面与本地交互逻辑，不改 SET/ACK 协议与固件；静态 IP 写入行为不变；动态 IP 为界面占位、未实现功能。
- 验证方式：`python -m py_compile` 输出 `SYNTAX_OK`；检查 `App` 具备 `_show_detail`/`_on_select` 方法，源码含静态/动态与预留标识。未启动 GUI 做人工点击验证（无显示环境）。
- 遗留风险 / 待办：动态 IP(DHCP) 仅界面选项，固件未实现；预留区为空占位，后续需按需求填充；未做 GUI 实际运行验证；`__pycache__` 缓存仍未清理（工具边界限制）。
- 关联记录：33

### [35] 广播地址改回有限广播并增加网卡选择
- 时间：2026-11-24 12:40
- 类型：功能新增
- 触发来源：用户「广播的地址能不能改一下，使用ff.ff.ff.ff，支持选择网卡。」
- 问题现象 / 需求描述：此前为规避多网卡走错网卡问题改用定向广播 `192.168.97.255` 并硬编码网卡；用户要求改回有限广播 `255.255.255.255`（ff.ff.ff.ff）并允许界面选择出口网卡。
- 根本原因：定向广播+硬编码网卡缺乏灵活性；绑定具体网卡后有限广播的出口由绑定地址决定，可同时满足两点。
- 改动位置：`find_dev/host/device_finder.py`（常量、网卡枚举函数、socket 初始化、网卡下拉与切换、状态提示）；`find_dev/Core/WORKLOG.md`（本条及汇总表行）
- 改动内容：
  - `BROADCAST_IP` `192.168.97.255` → `255.255.255.255`；`BIND_IP` 保留为初始绑定地址 `192.168.97.122`。
  - 新增 `list_local_ips()`：用 `socket.getaddrinfo(gethostname(), AF_INET)` 枚举本机 IPv4（排除 127.x），并加入 `0.0.0.0`（系统自动）。
  - `__init__`：新增 `self.local_ips`/`self.bind_ip`，改由 `_open_socket()` 建立 socket 并绑定 `self.bind_ip`（不再直接用常量绑定）。
  - 新增 `_open_socket()`（建 socket + 开广播 + 绑定 + 启动接收线程）与 `_on_nic_change()`（关闭旧 socket → 更新 bind_ip → 重开 socket → 提示并重新发现）。
  - 工具栏「端口」后新增「网卡」下拉框（`<<ComboboxSelected>>` 绑定切换）；状态提示中的网卡名改为 `self.bind_ip`。
- 影响范围：仅上位机；广播目标与出口网卡可运行时切换；协议与固件不变；`0.0.0.0` 选项由系统自动选网卡（可能走默认路由）。
- 验证方式：`python -m py_compile` 输出 `SYNTAX_OK`；枚举得到 `['0.0.0.0','172.16.1.247','192.168.5.1','192.168.71.1','192.168.97.122']`；逐个绑定非 0 网卡向 `255.255.255.255:50000` 发送均 `SEND_OK`，源地址与绑定 IP 一致，确认有限广播从所选网卡发出。未启动 GUI 验证下拉切换交互（无显示环境）。
- 遗留风险 / 待办：`getaddrinfo(gethostname())` 未列出 `169.254.x` 自动地址（如设备接在该网段需手动改 `BIND_IP`）；选 `0.0.0.0` 仍可能走默认路由导致广播不到设备；切换网卡后旧接收线程随旧 socket 关闭自然退出，未显式 join；未做实板联调。
- 关联记录：32、34

### [36] 修复改 IP 后再次配置失效（SET 改广播下发并即时同步）
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户需求原文摘要「为什么网络修改不了，增加上修改子网掩码的功能，和DHCP的功能，单片机和上位机同步。」
- 问题现象 / 需求描述：首次写入 IP 后再次点「写入配置」无响应；界面仍显示旧地址，用户感知为网络参数改不了。
- 根本原因：上位机 `set_device_ip()` 用 `d['ip']`（上次 REPLY 缓存的旧地址）单播下发 SET；IP/掩码变更后该地址失效，SET 到不了设备；ACK 后仅提示「下一次发现将显示新地址」，未更新本地缓存，界面看不到变化。
- 改动位置：`find_dev/host/device_finder.py` 的 `set_device_ip()`（310-345 行）、`_on_ack()`（377-403 行）
- 改动内容：`self.sock.sendto(frame, (d['ip'], DEFAULT_PORT))` → `self.sock.sendto(frame, (BROADCAST_IP, DEFAULT_PORT))`（仍按 target MAC 精确匹配，只有目标设备应用）；新增 `self.pending_params` 缓存本次下发的 ip/sn/gw/模式，ACK 成功后立即写回 `self.devices[mac]`、刷新详情并调用 `discover()` 复核实际状态；删除「动态 IP 固件暂未实现」的提前返回。
- 影响范围：上位机 SET 下发与 ACK 处理；固件 `discovery_parse()` 的 target MAC 校验逻辑不变；帧结构与校验算法不变。
- 验证方式：`python -m py_compile device_finder.py` 退出码 0；协议自测输出 `PROTOCOL_OK set=13 reply=51`；未启动 GUI 与实板联调。
- 遗留风险 / 待办：广播 SET 同网段所有设备都会收到并按 target MAC 过滤，若两台设备 MAC 配置成相同会同时被改；未做实板验证。
- 关联记录：33、38、39

### [37] 新增固件统一网络配置模块与轻量 DHCP 客户端
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「增加上修改子网掩码的功能，和DHCP的功能，单片机和上位机同步。」
- 问题现象 / 需求描述：原固件只有 `s_ip[4]` 与 `setSIPR()`，掩码/网关无写入路径，`wizchip_conf.h` 虽有 `NETINFO_DHCP` 枚举但工程无 `dhcp.h/c`，DHCP 无法工作；网络状态散落在多处，无法与上位机同步。
- 根本原因：缺少统一的网络参数状态源，且缺少 DHCP 客户端实现。
- 改动位置：新增 `Core/Inc/netcfg.h`、`Core/Src/netcfg.c`；修改 `Core/Src/main.c`（include、初始化、主循环）
- 改动内容：
  - 新增 `netcfg_t`（mode/status/ip/sn/gw/dns）与 `netcfg_init()`、`netcfg_poll()`、`netcfg_get()`、`netcfg_apply()`；REPLY/HTTP/JSON 统一从 `netcfg_get()` 读取。
  - `netcfg_apply(NETCFG_MODE_STATIC)` 调用 `setSIPR()/setSUBR()/setGAR()` 写入真实寄存器；`NETCFG_MODE_DHCP` 先保存静态回退参数、把 SIPR 置 0 并启动 DHCP。
  - 新增 socket2  UDP/68 的轻量 DHCP DORA 状态机（DISCOVER→OFFER→REQUEST→ACK），校验 magic cookie 与 xid，解析 option 1/3/54，成功后写入 IP/掩码/网关；4s 超时重试 3 次，失败置 `NETCFG_ST_DHCP_FAIL`，允许上位机重新下发静态参数恢复。
  - `main.c`：新增 `#include "netcfg.h"`、`netcfg_init()`（位于 `w5500_port_init()` 之后）、主循环 `netcfg_poll()`。
- 影响范围：固件网络初始化流程、主循环、REPLY/HTTP 状态来源；占用 socket2 与 UDP 68 端口；HTTP socket0、发现 socket1 不受影响。
- 验证方式：`gcc -fsyntax-only -DSTM32F103xB` 检查 `netcfg.c`/`main.c` 退出码 0（仅 CMSIS 头文件既有指针宽度警告）；未上板验证 DHCP 交互。
- 遗留风险 / 待办：未实现租约续租（T1/T2）与 option 6 DNS 解析，绑定后不再续租；DHCP 未获取成功时 IP 为 0.0.0.0，需靠广播 REPLY/SET 恢复；未做实板 DHCP 服务器联调。
- 关联记录：38、39、40、41

### [38] 协议扩展：REPLY 回报掩码/网关/DHCP，SET 携带完整网络参数
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「增加上修改子网掩码的功能，和DHCP的功能，单片机和上位机同步。」
- 问题现象 / 需求描述：REPLY payload 仅 42B（无掩码/网关/DHCP），SET 仅 4B IP，上位机无法获知或下发完整网络状态。
- 根本原因：协议字段不足，且固件没有可用的状态源（见记录 37）。
- 改动位置：`Core/Src/discovery.c`（文件头协议注释、宏定义、`discovery_parse()`、`discovery_build_reply()`、`discovery_apply_net()`、`discovery_poll()`）、`Core/Inc/discovery.h`（注释）
- 改动内容：
  - REPLY payload 42B → 51B：保留前 42B 格式不变，末尾追加 `sn[4] gw[4] dhcp[1]`；`DISC_REPLY_PAYLOAD = 51`，校验和覆盖长度同步；固件版本 `0x0100` → `0x0101`。
  - SET payload 兼容两种长度：`4B ip[4]`（旧格式，沿用当前掩码/网关/模式）与 `13B dhcp[1] ip[4] sn[4] gw[4]`（新格式）；`discovery_parse()` 两种 plen 均接受，target 仍必须精确匹配本机 MAC。
  - `discovery_apply_ip()` → `discovery_apply_net()`：由只写 `setSIPR()` 改为按模式调用 `netcfg_apply()` 统一写入 IP/掩码/网关或启动 DHCP。
  - ACK 仍在应用参数前发出（DHCP 切换会把 SIPR 置 0，先回 ACK 保证能送达），随后应用参数并触发 LED 常亮。
- 影响范围：固件发现协议；旧上位机读前 42B 仍可用；新上位机按 51B 解析；`w5500.h` 不再被 discovery.c 直接包含。
- 验证方式：`gcc -fsyntax-only` 通过；Python 协议自测同时构造 13B SET、51B REPLY 与旧版 42B REPLY，三者 `parse_frame()` 均通过，输出 `PROTOCOL_OK set=13 reply=51`。
- 遗留风险 / 待办：ACK 表示「命令已接受」而非「参数已生效」，实际生效状态需靠 ACK 后重新发现确认；未做实板联调。
- 关联记录：33、36、37、39

### [39] 上位机增加掩码/网关/DHCP 配置并同步显示网络状态
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「点击之后进行对应的列表中的选项，在侧面显示具体的信息……应该有静态和动态的IP选项……增加上修改子网掩码的功能，和DHCP的功能，单片机和上位机同步。」
- 问题现象 / 需求描述：右侧详情只有 IP 等基础字段；配置区只有「新 IP」和静态/动态下拉，动态选项点击后提示未实现；其他配置区保持留空。
- 根本原因：UI 与协议都未覆盖掩码/网关/DHCP 字段，且未消费 REPLY 扩展数据。
- 改动位置：`find_dev/host/device_finder.py`（协议常量、`__init__`、`_build_ui()`、`_show_detail()`、`set_device_ip()`、`_on_reply()`、`_on_ack()`）
- 改动内容：
  - 新增 `REPLY_EXT_FMT='>4s4sB'`（9B）与 `REPLY_EXT_LEN`；`DHCP_TEXT={0:'静态',1:'动态(DHCP)'}`；`STATUS_TEXT` 扩展 DHCP 获取中/失败文案；固件版本解析沿用 `v1.1`。
  - 详情区新增「子网掩码/网关/IP 模式」三个只读字段；配置区新增「子网掩码」「网关」输入框，IP 模式下拉值改为 `('静态','动态(DHCP)')`，保留「其他配置（预留）」空白区不变。
  - `_on_reply()` 按 51B 解析扩展字段并写入 `self.devices[mac]`；选中设备时只刷新只读详情（`sync_inputs=False`），避免自动发现覆盖用户正在编辑的输入。
  - `set_device_ip()` 组装 `dhcp[1]+ip[4]+sn[4]+gw[4]` 13B payload，空输入框回退到设备当前值；`_on_ack()` 成功后立即更新缓存、详情并重新发现。
- 影响范围：上位机 GUI 布局与 REPLY/SET 编解码；协议扩展见记录 38；网卡选择与广播地址保持不变。
- 验证方式：`python -m py_compile` 退出码 0；协议自测通过；未启动 GUI 验证交互（无显示环境）。
- 遗留风险 / 待办：basedpyright 在 `list_local_ips()` 的 `info[4][0]` 上报 2 个既有类型错误（运行期实际为 str），非本次改动引入，未处理；未做实板联调。
- 关联记录：34、36、37、38

### [40] HTTP 页面与 JSON 接口改为读取当前网络参数
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户需求「单片机和上位机同步。」
- 问题现象 / 需求描述：HTTP 测试页表格写死 `192.168.97.138`，`/api/status` 同样返回硬编码 IP；改 IP/掩码后网页仍显示旧地址，与上位机、固件实际状态不一致。
- 根本原因：网页与状态接口使用编译期常量而非运行时网络配置。
- 改动位置：`Core/Src/http_server.c`（文件头注释、`HTTP_PAGE` 表格与 `refresh()`、`http_handle_request()` 的 `/api/status` 分支）
- 改动内容：
  - 表格首行由固定 IP 改为 `<td id='ip'>`，新增掩码/网关/IP 模式三行；`refresh()` 从 JSON 读取 `ip/sn/gw/dhcp` 并填充。
  - `/api/status` 由 `{"ip":"192.168.97.138",...}` 改为从 `netcfg_get()` 动态格式化 `%u.%u.%u.%u` 的 ip/sn/gw 及 `dhcp`/`status` 字段；`body[128]` 扩为 `body[256]`。
- 影响范围：HTTP 状态接口字段新增（旧字段 `requests/uptime/link` 保留）；依赖 `netcfg.h`；网页地址随配置变化。
- 验证方式：`gcc -fsyntax-only` 通过；未启动设备用浏览器验证页面渲染。
- 遗留风险 / 待办：`/api/status` 未输出 DNS；未做实板浏览器验证。
- 关联记录：37、38

### [41] Keil 工程加入 netcfg.c 源文件
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：新增 `netcfg.c` 后需让 Keil 参与编译。
- 问题现象 / 需求描述：新增固件源文件未加入 `.uvprojx`，Keil 构建会缺少 `netcfg_init()/netcfg_poll()` 定义导致链接失败。
- 根本原因：Keil 工程文件需显式列出源文件。
- 改动位置：`MDK-ARM/NET_test.uvprojx` 的 `Network/W5500` 分组（527 行附近）
- 改动内容：在 `w5500_port.c` 之后新增 `<FileName>netcfg.c</FileName>` 与 `<FilePath>../Core/Src/netcfg.c</FilePath>` 条目。
- 影响范围：Keil 构建输入；不影响已有分组与头文件路径。
- 验证方式：文本检查 XML 结构与既有条目一致；未运行 Keil 完整构建（本机无 Keil 环境）。
- 遗留风险 / 待办：未做 Keil 全量编译与烧录验证。
- 关联记录：37

### [42] 设备列表改为常驻，不再按超时自动消失
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：缺陷修复
- 触发来源：用户需求「上位机中的列表的会自己消失，在单次扫描中的就一直保持着设备的列表，方便进行修改。」
- 问题现象 / 需求描述：扫描到的设备约 10 秒后从列表自动消失，用户还没来得及选中修改配置就找不到设备。
- 根本原因：`_poll()` 每轮（3 秒）按 `now - d['ts'] > STALE_SEC`（`STALE_SEC = 10`）删除超过 10 秒未回 REPLY 的设备；设备在“自动发现”关闭或临时无响应时即被清除。
- 改动位置：`find_dev/host/device_finder.py`（常量区、`_poll()`、`_on_reply()`）
- 改动内容：
  - 删除 `STALE_SEC = 10` 常量。
  - `_poll()` 删除 `stale = [...]`、`for mac in stale: del self.devices[mac]`、`if stale: self._render()` 整段；保留每 3 秒的队列消费、自动发现与定时调度。
  - `_on_reply()` 删除不再使用的 `'ts': time.time()` 字段；`'last'`（最后响应时间）保留用于显示。
  - 设备现在只在用户点击「清空」时移除；关闭「自动(3s)」后即为单次扫描结果的固定列表。
- 影响范围：仅上位机设备列表生命周期；REPLY/SET/ACK 协议与固件无变化；自动发现开启时仍会刷新已存在设备的信息（含 IP/掩码/DHCP），但不再删除。
- 验证方式：`python -m py_compile device_finder.py` 退出码 0；全文搜索 `STALE_SEC`、`['ts']`、`'ts':` 均为 0 处残留；未启动 GUI 验证交互（无显示环境）。
- 遗留风险 / 待办：设备离线或改 IP 后旧条目会一直保留，需用户手动「清空」或重新发现覆盖；未做 GUI 实机验证。
- 关联记录：29、36、39

### [43] 固件协议升级 v2：CRC16 校验与全广播应答
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户选择「全广播 + CRC16（按上面 4 步改，协议升 v2）」
- 问题现象 / 需求描述：v1 用 16 位累加和检错能力弱；设备 REPLY/ACK 单播，掩码配错时上位机收不到，无法救砖；需要双方全广播并升级校验。
- 根本原因：累加和对多位错误漏检；单播回包依赖正确的 IP/掩码路由。
- 改动位置：`Core/Src/discovery.c`（文件头协议注释、版本宏、静态状态、校验函数、`discovery_parse()`、`discovery_build_reply()`、`discovery_build_ack()`、`discovery_poll()`）
- 改动内容：
  - 新增 `DISC_VERSION_V1=1`、`DISC_VERSION_V2=2`，当前版本改为 v2；固件版本 `0x0101` → `0x0102`。
  - 新增 `discovery_crc16(data,len,crc)`（多项式 0x1021、初值 0xFFFF，支持分段）与 `discovery_calc(ver,head,head_len,payload,plen)`：v2 用 CRC16，v1 沿用累加和；头 26B 与 payload 分段计算，跳过中间 2B 校验字段。
  - `discovery_parse()` 接受 v1/v2，按版本校验，校验通过后把版本存入 `s_req_ver`；`discovery_build_reply()`/`discovery_build_ack()` 用 `s_req_ver` 填版本并计算对应校验，**应答版本跟随请求版本**，v1 旧上位机仍可互通。
  - REPLY/ACK 目标 IP 由 `peer_ip` 改为广播 `s_bcast = {255,255,255,255}`，目标端口仍用 `peer_port`；`peer_ip` 因 ioLibrary `recvfrom()` 要求非空缓冲而保留并加 `(void)` 消除未用警告。
  - `discovery_poll()` 在 LED 快闪前忽略 magic 匹配且类型为 REPLY/ACK 的广播包，避免全广播环境下其它设备的应答误触发 LED 诊断。
- 影响范围：固件发现协议版本升 v2；v1 请求仍被接受并以 v1 应答；REPLY/ACK 变为二层广播，同网段所有主机可见；socket1 资源不变。
- 验证方式：`gcc -fsyntax-only -DSTM32F103xB` 退出码 0；用独立 C 实现对同一测试向量算出 `FW_CRC=DAA4`，与上位机 Python 实现的 `crc=DAA4` 一致；未上板抓包验证广播回包。
- 遗留风险 / 待办：全广播会增加同网段流量；未实现租约续租外的其它协议扩展；未做实板抓取 REPLY/ACK 广播包与 v1 旧上位机互通验证。
- 关联记录：37、38、44

### [44] 上位机协议升级 v2：CRC16、固定端口绑定与随机 session
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户选择「全广播 + CRC16（按上面 4 步改，协议升 v2）」
- 问题现象 / 需求描述：设备改为广播回 REPLY/ACK 后，上位机原绑定随机端口（`bind(ip,0)`）收不到；需同步升级校验算法并避免多实例 session 撞车。
- 根本原因：UDP 广播回包投递到固定端口 50000；原 session 由 `time.time()` 生成，同一秒启动的实例可能相同；累加和检错弱。
- 改动位置：`find_dev/host/device_finder.py`（docstring、imports、版本常量、`_crc16()`/`_calc()`、`build_frame()`、`parse_frame()`、`__init__()`、`_open_socket()`、`_build_ui()`）
- 改动内容：
  - 新增 `import random`、`VERSION_V1=1`、`VERSION=2`；新增 `_crc16(data,crc=0xFFFF)` 与 `_calc(ver,head,payload)`（v2 分段 CRC，v1 累加和）。
  - `build_frame(..., ver=VERSION)` 按版本计算校验；`parse_frame()` 接受 v1/v2 并调用 `_calc(ver,...)` 校验，返回值新增 `version` 字段；发送默认 v2。
  - `_open_socket()` 改为 `setsockopt(SO_REUSEADDR,1)` 后 `bind((self.bind_ip, DEFAULT_PORT))`，以接收设备广播回包。
  - `self.session` 由 `int(time.time()) & 0xFFFFFFFF` 改为 `random.getrandbits(32)`。
  - 工具栏「端口」输入框改为固定文字 `端口：50000（固定）`（端口必须固定才能收到广播回包）；`self.port` 保留供 `discover()` 读取目标端口。
  - 修复自测发现的实际缺陷：`_calc()` 中分段 CRC 参数顺序由 `_crc16(_crc16(head), payload)` 更正为 `_crc16(payload, _crc16(head))`，否则构造 v2 帧会抛 `TypeError`。
- 影响范围：上位机编解码、socket 绑定、session 生成、工具栏 UI；同一 PC 多实例依赖 `SO_REUSEADDR`，session 随机化降低串包概率；旧版固件（v1）仍可解析上位机下发的 v1 帧。
- 验证方式：`python -m py_compile` 退出码 0；自测脚本验证分段 CRC 与独立查表实现一致、v2 帧可解析、v1 帧兼容、篡改 payload 后校验失败，输出 `V2_PROTOCOL_OK crc=DAA4`；临时测试脚本与编译产物已删除。
- 遗留风险 / 待办：固定端口 50000 被其它程序占用时 `bind()` 会抛异常，未做捕获提示；v1 兼容仅在上位机解析侧验证，未与真实旧固件联调；未启动 GUI 验证交互（无显示环境）。
- 关联记录：36、38、42、43

### [45] 固件新增设置设备名称消息（GBK / 限长 32B）
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「可以修改一个设备的信息，可以使用中文的内容，使用GBK的编码，限制整体的字符的长度。」
- 问题现象 / 需求描述：`REPLY` 的 `name[32]` 是编译期常量，上位机只能读不能改；需要支持中文名称并按 GBK 编码、限制长度。
- 根本原因：设备名称无写入路径且为 `const`，协议中没有对应命令。
- 改动位置：`Core/Src/discovery.c`（文件头协议注释、消息类型宏、`DISC_NAME_MAX`、静态名称缓冲、`discovery_parse()`、`discovery_apply_name()`、`discovery_poll()`、固件版本）
- 改动内容：
  - 新增 `MSG_SET_NAME=5u` 与 `DISC_NAME_MAX=32u`；`s_dev_name` 由 `const uint8_t[32]` 改为可写缓冲，初值保持 `STM32F103-W5500-WebFont`。
  - `discovery_parse()` 接受 `MSG_SET_NAME`，要求 payload 长度 1..32B，`target` 与 `MSG_SET` 一样必须精确匹配本机 MAC；校验沿用 v1 累加和 / v2 CRC16 的版本协商。
  - 新增 `discovery_apply_name(name,len)`：先清零 32B 再拷贝，不足补 0；`discovery_poll()` 增加处理分支，广播回 ACK 后应用，ACK 仍跟随请求版本。
  - `REPLY` 的 `name[32]` 直接回报当前缓冲内容，上位机按 GBK 解码即可显示中文。
  - 固件版本 `0x0102` → `0x0103`（协议版本仍为 v2）。
- 影响范围：固件发现协议新增消息类型；名称仅在 RAM 生效，掉电恢复默认名；不改变 IP/掩码/DHCP 配置逻辑与 REPLY 长度（仍 51B）。
- 验证方式：`gcc -fsyntax-only -DSTM32F103xB` 退出码 0；名称未做实板写入与掉电恢复验证。
- 遗留风险 / 待办：名称未持久化到 Flash，掉电丢失；固件不会校验 GBK 字节序列是否合法（仅按长度收），非法编码由上位机写入侧拒绝；未做实板联调。
- 关联记录：38、43、46

### [46] 上位机增加设备名称编辑与 GBK 限长
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：功能新增
- 触发来源：用户需求「可以修改一个设备的信息，可以使用中文的内容，使用GBK的编码，限制整体的字符的长度。」
- 问题现象 / 需求描述：右侧详情只能显示设备名，无法修改；需要支持中文并按 GBK 编码、限制整体字节长度。
- 根本原因：UI 无名称输入框，协议解析无 `MSG_SET_NAME`，`decode_text()` 优先 UTF-8 不适合 GBK 设备名。
- 改动位置：`find_dev/host/device_finder.py`（docstring、常量、`decode_text()`、`_build_ui()`、`_limit_name()`、`set_device_name()`、`_show_detail()`、`set_device_ip()`、`_on_ack()`）
- 改动内容：
  - 新增 `MSG_SET_NAME=5`、`NAME_MAX=32`；docstring 补充 SET_NAME 帧格式。
  - `decode_text()` 解码顺序由 `('utf-8','gbk')` 改为 `('gbk','utf-8')`，使固件下发的 GBK 中文正确显示；ASCII 默认名仍正常。
  - 「参数配置」区新增「设备名称」输入框、「写入名称」按钮与提示 `GBK 编码，≤32 字节（约 16 汉字）`；「其他配置（预留）」区保持空白不变。
  - 新增 `_limit_name()`：通过 `StringVar.trace_add('write')` 按 GBK 字节数实时截断到 32B，截断在双字节中间时用 `decode('gbk','ignore')` 丢弃残字节；`status` 未创建时用 `getattr` 保护。
  - 新增 `set_device_name()`：严格 `encode('gbk')`（非法字符提示）、校验 1..32B、广播下发 `MSG_SET_NAME` 帧、记录 `pending_params=('name',text)`。
  - `pending_params` 改为带标签元组：`('net',ip,sn,gw,is_dhcp)` 与 `('name',text)`，`_on_ack()` 按标签分别更新网络参数或名称；`_show_detail(sync_inputs=True)` 同步名称输入框。
- 影响范围：上位机 UI、名称编解码与 ACK 处理；网络参数写入路径因 `pending_params` 加标签而同步调整；协议与 v2 CRC16 不变。
- 验证方式：`python -m py_compile` 退出码 0；自测脚本验证中文/中英混合/16 汉字边界的 GBK 帧往返、32B 补零解码、ASCII 默认名解码、17 汉字超限判定，输出 `NAME_GBK_OK max=32B`；临时脚本已删除。
- 遗留风险 / 待办：名称仅存在于固件 RAM，掉电恢复默认名；未启动 GUI 验证输入实时截断交互（无显示环境）；未做实板写入验证。
- 关联记录：39、43、45

### [47] 删除上位机重复的设备详情区
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：重构优化
- 触发来源：用户需求「其实不用单独显示一遍设备的详情内容，，因为参数里面的内容就是显示的设备内容。」
- 问题现象 / 需求描述：右侧「设备详情」区与「参数配置」区重复显示 IP/掩码/网关/IP 模式/名称，界面冗余；MAC、端口、工作模式、固件、状态、最后响应在左侧列表已有列，无需再显示一遍。
- 根本原因：早期为展示设备全量信息单独建了只读区，后续参数配置区补齐了同名字段，形成重复。
- 改动位置：`find_dev/host/device_finder.py`（`__init__()`、`_build_ui()`、`_show_detail()`、`_on_reply()`）
- 改动内容：
  - `__init__()` 删除 `self.detail_vars = {}`。
  - `_build_ui()` 删除整个「设备详情」LabelFrame 及 11 个只读字段（名称/MAC/IP/掩码/网关/模式/端口/固件/状态/最后响应），配置区改为唯一显示入口并更新注释。
  - `_show_detail()` 去掉 `sync_inputs` 参数：选中设备时把 `ip/sn/gw/dhcp/name` 填入配置输入框；未选中或清空时把这些输入项置空、模式复位为「静态」。
  - `_on_reply()` 删除 `if mac == self.selected_mac: self._show_detail(mac, sync_inputs=False)`，自动发现不再刷新只读区（该区已删除），也避免覆盖用户正在编辑的输入。
  - `_on_ack()`、`_render()`、`_on_select()` 的 `_show_detail()` 调用保持不变（参数已兼容）。
- 影响范围：仅上位机右侧 UI 布局与选中回填逻辑；左侧列表列、设备发现/配置/名称写入协议均不变；`MAC/端口/模式/固件/状态/最后响应` 仍可从左侧列表查看。
- 验证方式：`python -m py_compile` 退出码 0；全文搜索 `detail_vars`、`sync_inputs` 均为 0 处残留；未启动 GUI 验证布局（无显示环境）。
- 遗留风险 / 待办：选中设备后自动发现刷新设备 IP 时，配置输入框不会跟着更新（需重新点选设备），这是为避免覆盖用户编辑而有意为之；未做 GUI 实机验证。
- 关联记录：34、39、46

### [48] 归档 find_dev 开发进度（记录 29-47）
- 时间：2026-11-24（本轮对话，精确时分未记录，待确认）
- 类型：配置调整
- 触发来源：用户需求「现在的进度可以进行一次归档了。」
- 问题现象 / 需求描述：记录 29-47 的固件与上位机改动尚未纳入版本控制；工作区有 5 个已跟踪文件修改、4 个新增源码文件与 `host/` 目录未跟踪，需按记录 28 的方式做一次 Git 备份归档。
- 根本原因：上次归档停留在提交 `860123e`（记录 28），此后 19 条工作记录对应的代码变更未提交。
- 改动位置：`Core/WORKLOG.md`（新增记录 48）、Git 暂存区与提交历史
- 改动内容：
  - 归档前确认 `host/` 目录仅含 `device_finder.py`，无 `__pycache__` 等临时产物；暂存区为空。
  - 本次纳入归档的已跟踪修改：`Core/Src/http_server.c`、`Core/Src/main.c`、`Core/WORKLOG.md`、`MDK-ARM/NET_test.uvprojx`。
  - 本次纳入归档的新增文件：`Core/Inc/discovery.h`、`Core/Inc/netcfg.h`、`Core/Src/discovery.c`、`Core/Src/netcfg.c`、`host/device_finder.py`。
  - 覆盖功能：UDP 发现与广播应答、LED 三态诊断、网卡选择与有限广播、网络参数配置与 DHCP 客户端、设备列表常驻、协议 v2（CRC16 + 全广播）、GBK 中文设备名、移除重复详情区。
- 影响范围：仅版本控制历史；不改变任何源码行为；`.gitignore` 已排除 Keil 编译产物与调试器本地配置。
- 验证方式：归档后执行 `git status --short` 确认工作区干净、`git log -1` 确认提交号；推送状态见「遗留风险」。
- 遗留风险 / 待办：本地提交与远端推送是否同时完成，以本轮实际执行结果为准；未做归档后重新编译验证（无 Keil 环境）。
- 关联记录：28、47

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
| 27 | 配置调整 | 提交并推送 base/ 目录重构到 GitHub | Git 提交历史、GitHub 远端仓库 | 已完成 |
| 28 | 配置调整 | 提交并推送 find_dev 开发版本备份到 GitHub | Git 提交历史、GitHub 远端仓库、`find_dev/Core/WORKLOG.md`、`.gitignore` | 已完成 |
| 29 | 功能新增 | 新增 UDP 设备发现上位机与局域网设备列表 | `find_dev/host/device_finder.py` | 已完成 |
| 30 | 功能新增 | 固件实现 UDP 发现监听与 REPLY 应答 | `find_dev/Core/Inc/discovery.h`, `find_dev/Core/Src/discovery.c`, `main.c`, `NET_test.uvprojx` | 已完成 |
| 31 | 功能新增 | 新增 LED 三态诊断指示定位发现链路 | `find_dev/Core/Src/discovery.c`, `main.c` | 已完成 |
| 32 | 缺陷修复 | 修复多网卡环境下 UDP 广播走错网卡 | `find_dev/host/device_finder.py` | 已完成 |
| 33 | 功能新增 | 增加通过 UDP 配置设备 IP 地址的功能 | `device_finder.py`, `discovery.c` | 已完成 |
| 34 | 功能新增 | 上位机重构为左右分栏，增加设备详情与配置项预留 | `find_dev/host/device_finder.py` | 已完成 |
| 35 | 功能新增 | 广播地址改回有限广播并增加网卡选择 | `find_dev/host/device_finder.py` | 已完成 |
| 36 | 缺陷修复 | 修复改 IP 后再次配置失效：SET 改广播下发并即时同步 | `find_dev/host/device_finder.py` | 已完成 |
| 37 | 功能新增 | 固件统一网络配置模块与轻量 DHCP 客户端 | `Core/Inc/netcfg.h`, `Core/Src/netcfg.c`, `Core/Src/main.c` | 已完成 |
| 38 | 功能新增 | REPLY 回报掩码/网关/DHCP，SET 携带完整网络参数 | `Core/Src/discovery.c`, `Core/Inc/discovery.h` | 已完成 |
| 39 | 功能新增 | 上位机增加掩码/网关/DHCP 配置并同步显示 | `find_dev/host/device_finder.py` | 已完成 |
| 40 | 缺陷修复 | HTTP 页面与 JSON 接口改为读取当前网络参数 | `Core/Src/http_server.c` | 已完成 |
| 41 | 配置调整 | Keil 工程加入 netcfg.c | `MDK-ARM/NET_test.uvprojx` | 已完成 |
| 42 | 缺陷修复 | 设备列表改为常驻，不再按超时自动消失 | `find_dev/host/device_finder.py` | 已完成 |
| 43 | 功能新增 | 固件协议 v2：CRC16 校验与全广播应答 | `Core/Src/discovery.c` | 已完成 |
| 44 | 功能新增 | 上位机协议 v2：CRC16、固定端口、随机 session | `find_dev/host/device_finder.py` | 已完成 |
| 45 | 功能新增 | 固件新增设置设备名称消息（GBK / 限长 32B） | `Core/Src/discovery.c` | 已完成 |
| 46 | 功能新增 | 上位机增加设备名称编辑与 GBK 限长 | `find_dev/host/device_finder.py` | 已完成 |
| 47 | 重构优化 | 删除上位机重复的设备详情区 | `find_dev/host/device_finder.py` | 已完成 |
| 48 | 配置调整 | 归档 find_dev 开发进度（记录 29-47） | `Core/WORKLOG.md`、Git 提交历史 | 已完成 |

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
| 27 | 配置调整 | 提交并推送 base/ 目录重构到 GitHub | Git 提交历史、GitHub 远端仓库 | 已完成 |
| 28 | 配置调整 | 提交并推送 find_dev 开发版本备份到 GitHub | Git 提交历史、GitHub 远端仓库、`find_dev/Core/WORKLOG.md`、`.gitignore` | 已完成 |

| 29 | 功能新增 | 新增 UDP 设备发现上位机与局域网设备列表 | `find_dev/host/device_finder.py` | 已完成 |

| 30 | 功能新增 | 固件实现 UDP 发现监听与 REPLY 应答 | `find_dev/Core/Inc/discovery.h`, `find_dev/Core/Src/discovery.c`, `main.c`, `NET_test.uvprojx` | 已完成 |

| 31 | 功能新增 | 新增 LED 三态诊断指示定位发现链路 | `find_dev/Core/Src/discovery.c`, `main.c` | 已完成 |

| 32 | 缺陷修复 | 修复多网卡环境下 UDP 广播走错网卡 | `find_dev/host/device_finder.py` | 已完成 |

| 33 | 功能新增 | 增加通过 UDP 配置设备 IP 地址的功能 | `device_finder.py`, `discovery.c` | 已完成 |

| 34 | 功能新增 | 上位机重构为左右分栏，增加设备详情与配置项预留 | `find_dev/host/device_finder.py` | 已完成 |

| 35 | 功能新增 | 广播地址改回有限广播并增加网卡选择 | `find_dev/host/device_finder.py` | 已完成 |

| 36 | 缺陷修复 | 修复改 IP 后再次配置失效：SET 改广播下发并即时同步 | `find_dev/host/device_finder.py` | 已完成 |

| 37 | 功能新增 | 固件统一网络配置模块与轻量 DHCP 客户端 | `Core/Inc/netcfg.h`, `Core/Src/netcfg.c`, `Core/Src/main.c` | 已完成 |

| 38 | 功能新增 | REPLY 回报掩码/网关/DHCP，SET 携带完整网络参数 | `Core/Src/discovery.c`, `Core/Inc/discovery.h` | 已完成 |

| 39 | 功能新增 | 上位机增加掩码/网关/DHCP 配置并同步显示 | `find_dev/host/device_finder.py` | 已完成 |

| 40 | 缺陷修复 | HTTP 页面与 JSON 接口改为读取当前网络参数 | `Core/Src/http_server.c` | 已完成 |

| 41 | 配置调整 | Keil 工程加入 netcfg.c | `MDK-ARM/NET_test.uvprojx` | 已完成 |

| 42 | 缺陷修复 | 设备列表改为常驻，不再按超时自动消失 | `find_dev/host/device_finder.py` | 已完成 |

| 43 | 功能新增 | 固件协议 v2：CRC16 校验与全广播应答 | `Core/Src/discovery.c` | 已完成 |

| 44 | 功能新增 | 上位机协议 v2：CRC16、固定端口、随机 session | `find_dev/host/device_finder.py` | 已完成 |

| 45 | 功能新增 | 固件新增设置设备名称消息（GBK / 限长 32B） | `Core/Src/discovery.c` | 已完成 |

| 46 | 功能新增 | 上位机增加设备名称编辑与 GBK 限长 | `find_dev/host/device_finder.py` | 已完成 |

| 47 | 重构优化 | 删除上位机重复的设备详情区 | `find_dev/host/device_finder.py` | 已完成 |

| 48 | 配置调整 | 归档 find_dev 开发进度（记录 29-47） | `Core/WORKLOG.md`、Git 提交历史 | 已完成 |
