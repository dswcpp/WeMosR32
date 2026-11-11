# 嵌入式实时股票技术分析模块（C99）

把 `stock_analyzer.h` 和 `stock_analyzer.c` 放进 STM32、ESP32、GD32 或 Linux 嵌入式工程，即可对指定股票的**已完成 K 线**或**行情 Tick**进行实时技术分析。无需 RTOS、线程、网络库或动态内存。

> 这个模块**只负责计算与分析**。A 股行情的联网获取、交易日历、时间同步、停牌识别、报警推送和屏幕显示，由外部业务模块完成。它不预测未来行情，也不下单。

## 文件

- `stock_analyzer.h`：C API、配置、数据结构、信号位定义。
- `stock_analyzer.c`：指标计算、信号判定、K 线聚合。
- `example.c`：两只股票分别采用 K 线输入及 Tick 输入的运行示例（价格全是模拟数据）。
- `test_stock_analyzer.c`：断言测试。

## 适配特点

- **C99**，只依赖 `stdint.h`、`math.h`、`string.h`。
- 不分配堆内存，没有网络和操作系统 API。每个股票使用**独立 `stock_analyzer_t` 实例**。
- 最大滚动窗口为 **32 根 K 线**；默认指标：EMA(5,20)、MACD(12,26,9)、RSI(14)、VWAP、BOLL(20,2)、前 20 根均量和前 20 根高低点突破。
- 纯 Tick 接口将数据按 `bar_seconds`（默认 60 秒）聚合为 K 线；每次 Tick 可计算**预览信号**，跨时间桶时产出**上一根正式收盘 K 线**。
- 输出 `ready` 位，表示某指标已达到预热需求。未就绪时不要解读该指标的数值。
- `score` 是透明规则评分（-100 到 +100），**不是概率，也不是买卖建议**。

## 编译

在电脑上用 GCC 测试：

```bash
gcc -std=c99 -O2 -Wall -Wextra example.c stock_analyzer.c -lm -o example
./example

gcc -std=c99 -O2 -Wall -Wextra test_stock_analyzer.c stock_analyzer.c -lm -o test_stock_analyzer
./test_stock_analyzer
```

在 STM32 裸机工程、ESP-IDF、PlatformIO 中，把 `.c` 加进构建列表，把 `.h` 加进 include 路径即可。使用的 `sqrt()` 需要数学库，GCC 链接通常加 `-lm`。在没有硬件 FPU 的 MCU 上，浮点计算可能有较高 CPU 成本，建议按需要降低行情输入频率；每个股票实例的实际 RAM 占用可以通过 `sizeof(stock_analyzer_t)` 查看。

## 方式 A：输入完成的 K 线

适合后端先取好行情、传输至 MCU，或 MCU 已经拿到交易所分钟 K 线的情形。

```c
#include "stock_analyzer.h"

static stock_analyzer_t stock_000001;

void stock_init(void)
{
    stock_analyzer_config_t cfg;
    stock_analyzer_default_config(&cfg);
    stock_analyzer_init(&stock_000001, &cfg);
}

void on_1min_bar(uint32_t start_timestamp_s, float o, float h,
                 float l, float c, float vol, float amount)
{
    stock_bar_t b = {
        .timestamp_s = start_timestamp_s,
        .open = o, .high = h, .low = l, .close = c,
        .volume = vol, .turnover = amount
    };
    stock_analyzer_result_t r;
    if (stock_analyzer_push_bar(&stock_000001, &b, &r) != STOCK_ANALYZER_OK)
        return;
    if (r.trend == STOCK_TREND_UP &&
        (r.signals & STOCK_SIGNAL_VOLUME_SPIKE)) {
        /* 向 OLED/串口/UI 推送“趋势偏强，放量”观察信号 */
    }
}
```

**`turnover` 的单位必须是“价格 × volume 单位”**。例如价格是元/股，`volume` 用股，则 `turnover` 用元。如果股票数据源返回“手”，先把手数乘以 100 转为股。若无法提供匹配的成交额，可设置 `turnover=0`，模块会用 `(最高+最低+收盘)/3 × 成交量` 来近似，误差取决于真实成交分布。

## 方式 B：输入实时 Tick

适合你自己通过 HTTP、MQTT、WebSocket、串口等拿到间隔 1～5 秒的价格快照。**务必提供每次新增的成交量**，不能把一天的累计成交量直接作为 `volume_delta` 传入。如果接口只返回累计成交量，则先计算本次累计值减去上次累计值；当天首次采样或遇到重置时单独处理基准，否则会产生虚假放量。

```c
static stock_analyzer_t g_stock;

void on_market_tick(uint32_t ts, float last_price, float new_shares)
{
    stock_tick_t tick = {ts, last_price, new_shares};
    stock_analyzer_result_t live, closed;
    stock_analyzer_status_t status =
        stock_analyzer_feed_tick(&g_stock, &tick, &live, &closed);
    if (status < 0) {
        /* 输入有误、时间倒退或与 K 线输入模式混用 */
        return;
    }
    /* live.is_closed == 0：正在形成的 K 线，信号可能反复变化 */
    if (status == STOCK_ANALYZER_BAR_COMPLETED) {
        /* closed.is_closed == 1：上一根 K 线已结束 */
    }
}
```

注意：调用前必须先 `stock_analyzer_init()`。同一实例不要混用 `push_bar()` 与 `feed_tick()`；若要切换输入模式，先 `stock_analyzer_reset_all()`。

可在 11:30、15:00 或其它会话结束时调用：

```c
stock_analyzer_result_t final_bar;
if (stock_analyzer_flush_tick(&g_stock, &final_bar) ==
    STOCK_ANALYZER_BAR_COMPLETED) {
    /* 最后一根 K 线已经完成 */
}
```

**Tick 处理约定：**

- `timestamp_s` 为 Unix 秒数；模块按 `bar_seconds` 的整倍数分桶，时间戳必须非递减。无需时区转换，但需要给出正确的 Unix 时间。
- 假设 Tick 的 `price` 是最后成交价、`volume_delta` 是本次新增成交股数。相同时间桶的 Tick 会更新开高低收、成交量和成交额。
- **没有成交 Tick 的时间桶不会补造 K 线**。中途断线后，调用方必须自行标记行情过期，不能认为最后一条分析仍然“实时”。
- `live` 是临时值，允许预览 MACD/RSI 等，但仅有历史已完成 K 线作为指标基础。临时均量异常信号被抑制，以避免因未完成成交量产生误报。盘中突破信号仍可能随价格回落消失。
- 如果采样的是 5 秒快照而不是逐笔成交，用“采样价格 × 区间新增成交量”估算 VWAP，并不等于交易所逐笔精确 VWAP。

## 交易日切换（重要）

系统不内置 A 股交易日历，也不会自动在日期变化时清空 VWAP。每天开盘前（建议先完成或丢弃前一天未完成的 Tick K 线）调用：

```c
stock_analyzer_reset_session(&g_stock);
```

这会清空**当日累计 VWAP**，同时保留 EMA/MACD/RSI 和历史 K 线状态。**不要在 11:30 午休时重置日内 VWAP**。如果之前的 K 线有跨日历史，用这个 API 让指标保持连续；首次启动时建议先按时间顺序送入足够的历史完成 K 线预热，至少 34 根才会获得默认 MACD 就绪信号。

## 位掩码读取与趋势

```c
if (r.ready & STOCK_READY_MACD) {
    /* r.macd, r.macd_signal, r.macd_histogram 可用 */
}
if (r.signals & STOCK_SIGNAL_MACD_CROSS_UP) {
    /* MACD 柱体从 <=0 穿越到 >0 的观察事件 */
}
```

`ready` 包括 `EMA`、`MACD`、`RSI`、`VWAP`、`BOLL`、`VOLUME`、`BREAKOUT`。`signals` 包括均线多空、价格与 VWAP 的相对位置、放量、突破、RSI 高/低、MACD 零轴柱体交叉、突破布林带。

透明启发式评分组成：EMA 快慢线 ±35、MACD 柱体 ±20、价格与 VWAP ±20、突破历史区间 ±15、RSI 位于 55 上方或 45 下方 ±10、放量时按 K 线阳阴 ±10，最后限幅为 [-100,100]。EMA 尚未就绪时趋势显示 `UNKNOWN`；评分 ≥45 定为 `UP`、≤-45 为 `DOWN`，其它为 `SIDEWAYS`。上述阈值是**可解释的工程初值**，不是经回测证明的最优参数。

## 嵌入式注意点

1. **多股监控：**`static stock_analyzer_t stocks[WATCH_COUNT]`；由股票代码映射对应实例，模块内部不接触股票名称。
2. **中断与线程：**模块不是线程安全的。建议网络收包中断/回调将行情放队列，由一个任务顺序调用分析 API；或者加互斥锁。
3. **内存：**实例内含固定数组，没有 `malloc/free`。实时预览会在函数栈上临时复制一个实例；如果 MCU 任务栈特别紧张，可只传 `live_out=NULL`，只计算完成 K 线，以消除预览副本开销。
4. **行情合法性：**模块检查正价格、OHLC 关系、有限浮点数、非负成交量和时间顺序，但仍需外层校验股票代码、价格复权方式、涨跌停、时间戳和数据源完整性。
5. **指标约定：**EMA 从首根收盘价初始化，MACD 信号线在慢 EMA 满足预热期后初始化；不同终端的 MACD/RSI 初始化规则可能略有差别。布林带采用总体标准差，VWAP 只按当前交易日累计。
6. **实时性：**本地算法开销很小，但整体延迟取决于行情源是否实时、采集间隔、网络传输和设备处理，不能把轮询间隔当作行情延迟承诺。
7. **风险：**算法仅对已经发生的价格与成交量进行统计，无法保证预测未来价格，更不能用单一规则评分直接进行自动交易。

## 许可

演示模块按 MIT License 提供，详见 `LICENSE`。如用于生产环境，请自行进行边界、内存及数值稳定性验证。

---

# v1.1 行情采集适配层：stock_market.h / stock_market.c

本版在原有分析算法基础上新增**纯 C99 的行情接入模块**。网络代码与分析算法解耦：STM32/GD32 使用 UART+DMA、ESP32 使用 Wi-Fi/HTTP，最终都调用相同的 `stock_market_feed()`。

## 新增源文件

| 文件 | 内容 | 编译平台 |
| --- | --- | --- |
| `stock_market.h` / `stock_market.c` | 输入合法性、逐字节解析、累计量差分、分钟线分析对接、交易日切换 | C99 / ARM GCC / ESP-IDF |
| `example_market.c` | 使用模拟行情演示逐字节收包、分析 | GCC |
| `test_stock_market.c` | 单元测试：格式/校验/错误、重复报价、成交量重置、跨日、时段 | GCC |
| `relay_eastmoney.py` | **实验性**东方财富接口适配器：HTTPS 请求并转成标准 ASCII 行情帧 | PC Python 3 |
| `bridge_uart.py` | 从 PC 中转服务获取行情后通过 UART 发送给 MCU | PC Python 3 + pyserial |
| `stm32_uart_example.c` | STM32/GD32 裸机 UART 主循环对接示例（不含具体 HAL 初始化） | STM32/GD32 |
| `esp32_http_example.c` | ESP-IDF 5.x HTTP 客户端对接示例（不含 Wi-Fi/NTP 初始化） | ESP32 |

## 核心数据规范（必须遵守）

```c
typedef struct {
    char symbol[7];                   // "000001"，固定6位ASCII数字
    uint32_t timestamp_s;             // 实际行情时间，UTC Unix秒
    float last_price;                 // 元/股
    uint64_t cumulative_volume_shares;// 当天累计成交量，股（不是手）
} stock_market_quote_t;
```

`stock_market_feed()` 将**当天累计股数**转成当前新增股数，再调用 `stock_analyzer_feed_tick()`：

- 当天第一次报价只建立成交量基准，因此**不会补入开盘到第一次接入之间的历史成交量**。
- 同一报价时间、价格、累计量都未变化时返回 `STOCK_MARKET_DUPLICATE`；同一秒有新价格/成交量仍可更新。
- 数据时间倒退、股票代码不匹配会返回负值；当日累计量反常下降时将差分设为 0，并置 `volume_resynced`。
- 跨交易日自动完成前一天最后一根 K 线（若尚未手动 flush），然后 `reset_session`，仅重置日内 VWAP 与部分 K 线，保留 EMA/RSI 等历史指标。
- 上午 11:30、下午 15:00 结束时调用 `stock_market_flush()`，避免正在形成的最后一根分钟 K 线丢失。
- `stock_market_is_cn_regular_session()` 只检查北京时间**周一至周五** 09:30–11:30 / 13:00–15:00，**不识别春节、国庆等节假日、临时休市**，由上层交易日历额外判断。
- 单次 Tick 只包含最新价和新增成交量。因此当前算法的 Tick-VWAP 基于价格采样加权的近似值，**不是直接读取交易所累计成交额计算的精确 VWAP**。想得到精确成交额口径，需要外部输入带成交额的完成 K 线，或升级算法的数据模型。
- 这里所谓“实时”是指实时处理**收到的报价**，而非保证报价来自交易所或零延迟；采集频率不能超过真实源更新时间。

## 最短 C 调用方式

```c
#include "stock_market.h"

static stock_market_watch_t g_stock;

void init_app(void)
{
    stock_market_init(&g_stock, "000001", NULL); // NULL = 默认分析参数
}

void on_quote(uint32_t ts, float price, uint64_t cumulative_shares)
{
    stock_market_quote_t q = {
        .symbol = "000001",
        .timestamp_s = ts,
        .last_price = price,
        .cumulative_volume_shares = cumulative_shares
    };
    stock_market_update_t u;
    stock_market_status_t rc = stock_market_feed(&g_stock, &q, &u);
    if (rc < 0 || rc == STOCK_MARKET_DUPLICATE) return;

    /* 预览信号，分钟未结束时可变化；仅作为行情观察信号 */
    if (u.live.ready & STOCK_READY_EMA) {
        /* u.live.trend, u.live.score, u.live.signals */
    }
    if (u.has_closed) {
        /* u.closed = 已结束的上一根分钟 K 线 */
    }
}
```

监控 N 只股票时，静态分配 `stock_market_watch_t watches[N]`，每只股票使用独立实例，外部根据代码路由；**不要把多只股票报价输入同一个 watch**。默认配置在 x86_64 GCC 环境里 `sizeof(stock_market_watch_t)` 约 **752 字节**，具体 RAM 以 MCU 交叉编译器结果为准。

## UART 线协议

推荐 UTF-8/ASCII 单行格式，不发送中文股票名：

```text
Q,000001,1794274200,10.25,1234500*HH\n
```

其中 `HH` 是 `Q` 起到 `*` 前所有字节的 XOR 校验和（2 位 16 进制）；解析器同时接受不带 `*HH` 的测试行。逐字节把 UART 环形缓冲区的数据交给 `stock_market_decoder_push()`，返回 `STOCK_MARKET_FRAME_READY` 才执行 `stock_market_feed()`。**不要在串口中断里运行浮点指标计算、strtod 或字符串解析**。

注意：XOR 只能检测部分传输差错，不能用于身份验证或抵御恶意篡改；生产链路可换为 CRC16/32 或认证协议。

## 电脑 GCC 编译、测试

```bash
gcc -std=c99 -O2 -Wall -Wextra example_market.c stock_market.c stock_analyzer.c -lm -o example_market
./example_market

gcc -std=c99 -O2 -Wall -Wextra test_stock_market.c stock_market.c stock_analyzer.c -lm -o test_stock_market
./test_stock_market
```

## ESP32 Wi-Fi 接入：实验性示例

1. 电脑运行 Python 3：`python relay_eastmoney.py --bind 0.0.0.0 --port 8765`。
2. PC 与 ESP32 在**受信任私有局域网**中，浏览器查看 `http://电脑局域网IP:8765/quote?symbol=000001` 能否返回 `Q,...*HH`。若接口返回 502，请检查第三方行情源响应格式，不要盲目忽略报错。
3. 修改 `esp32_http_example.c` 的 `RELAY_URL`（替换 `192.168.1.100`）。将该文件和两个模块的 `.c/.h` 添加进 ESP-IDF 工程。
4. 在自身 ESP-IDF 工程里**先连接 Wi-Fi、通过 SNTP 同步 UTC 时间**；再调用 `esp_stock_init()`，单独任务中每隔 5–30 秒调用 `esp_stock_poll_once()`。`esp_stock_flush_at_session_end()` 可在上午/下午会话末调用。
5. 代码仅展示客户端的采集、解析、校验和分析链路；**不包含** ESP-IDF 项目主入口、Wi-Fi 配网代码、证书/生产 HTTPS、OLED 驱动和交易日历，也没有在真实 ESP32 上编译或联机验证。ESP-IDF 示例使用的是私有局域网明文 HTTP，请勿将中转服务暴露到公网。

**数据源校准警告：** `relay_eastmoney.py` 基于非正式的东方财富 `qt/stock/get` 接口，使用 `fltt=2`，暂按 `f43=元/股`、`f47=手`、`f86=更新时间` 解析，再乘以 100 换算成股。第三方资料对 `f47` 单位存在不一致说明，**正式接入前务必与交易软件同一时刻的累计成交量、价格和更新时间逐项核对**；字段含义、时间延迟、访问限制及接口可用性随时可能改变。若源字段发生变化，**只修改适配层**，不要修改分析算法。

## STM32/GD32 UART 接入：实验性示例

1. PC 启动同一个行情中转：`python relay_eastmoney.py`。
2. 串口发送工具安装：`pip install pyserial`。
3. Windows 例子：`python bridge_uart.py --port COM3 --symbol 000001 --interval 5`。
4. STM32 端使用 UART DMA/IRQ 收包到环形缓冲区，然后在主循环/任务里调用 `market_uart_on_byte_from_main_loop()`，详见 `stm32_uart_example.c`。该文件没有预设 HAL、LL、寄存器库或具体 UART 引脚配置。

**风险边界：** 所有输出是技术指标和观察信号，不是未来价格预测或自动交易指令。任何实际交易前应验证数据延迟、复权口径、交易制度、错误率和回测表现。
