# OLED Stock Views Direction

This is an iteration of the existing 128x64 monochrome stock-monitor
direction, not a new visual concept. The user approved continuing this
interface with this request:

> 报价详情页需要好好设计“STOCK MONITOR”、“K4 BACK”、“K3 SYNC”这种就不要显示了，“WIFI ONLINE”等都不应该显示。我想看到的是该支股票详细的分析数据。且不用显示描述，好好设计数字布局。大小重点设计。给人一种人性化设计

## Visual Rules

- The stock code is the persistent left header; time and weekday occupy the
  top center without competing with the main values.
- Three small page marks replace textual navigation hints.
- Price, RSI, MACD, and score carry the strongest size hierarchy.
- Network state, refresh state, counters, and key instructions never appear.
- Missing values use aligned placeholders instead of explanatory sentences.
- Daily range, turnover, EMA spread, VWAP, Bollinger bands, and trend direction
  provide detailed numeric context.

## View Structure

```text
QUOTE                INDICATORS            ANALYSIS
600519      [*..]    600519      [.*.]     600519      [..*]
1234.56      ....    RSI       MACD        +42   SCORE   ^
+1.25%    +15.43     56        +0.12       O 12.00     P 12.10
H 1234.00 L 1210.00  -----------------     VWAP 12.20  +0.31%
VOL 1.2M  AMT 3.4B   EMA       VOL         BOLL 11.90  12.50
                     +0.42%    1.6X
```
