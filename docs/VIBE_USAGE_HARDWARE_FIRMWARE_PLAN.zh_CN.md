# Vibe Usage 双硬件 ESP-IDF 固件开发技术方案

版本：1.0 · 整理及源码核对日期：2026-09-04 · 交付对象：Coding Agent

目标仓库：`vibe-usage-ai-passport`、`vibe-usage-zectrix`。

本文是独立开发输入，无需继续读取原聊天。需求基线来自“调查Token采集方案”的最终决策：**硬件直接调用 Vibe Usage API；两个独立固件；共享业务契约；默认 SoftAP + Captive Portal；优先复用 `78/esp-wifi-connect`。** 后续章节中的“必须”是实现及验收要求；“默认”是本方案确定的产品参数；“待验证”必须转为有证据的测试结果，不能当作已经完成。

本文完成了公开源码与文档核对，未进行账号授权、真实用量请求、固件编译或真机测试。数值门槛属于本项目的开发目标，不代表上游或硬件厂商的性能保证。

## 1. 目标、非目标与交付边界

### 1.1 项目目标

1. 手机为设备配置家庭 Wi-Fi，随后通过 Vibe Usage Device Flow 绑定账号。
2. 设备通过 HTTPS 和 Bearer API Key 直接读取 `/api/usage`，显示 Today、7D、按 Agent/source 聚合的 Token 数量与占比。
3. 在设备保存最近 8 个自然日的聚合缓存，日常更新 Today，每日对最近 7 日做 reconciliation，修正迟到上传与服务端修订。
4. 网络、TLS、解析和服务异常时保留 Last Known Good（LKG），准确区分空数据、过期数据和授权失效。
5. AI Passport 提供彩色交互界面，保留官方 Recovery、identity 和小程序安装兼容性。
6. NOTE4 提供黑白电子纸常显界面，先完成常开模式，再完成经过电池实测的定时唤醒与休眠。
7. 两仓独立构建、发布；相同 API、缓存与测试契约，允许显示和电源实现独立演进。

### 1.2 非目标

- 不在硬件上解析 Codex、Claude、Grok、Kimi 等本机日志；采集、去重、上传由现有 Vibe Usage CLI/App 完成。
- 不建设 GitHub Pages、Exporter、VPS、中转 API 或公开的 usage JSON。GitHub Actions 只用于固件 CI/Release。
- 不实现新的账号、登录页面或设备授权协议；不要求用户手动输入 `vbu_` Key。
- 不显示会话、项目路径、主机名、Prompt、源码和模型明细；不实现订阅配额、余额或费用推断。
- P0 不做 BLE/SmartConfig 配网、语音、小智对话、NFC、灰阶 Dashboard、远程控制和自建 OTA。
- 不支持 NOTE4C；不承诺任意 ESP32 板卡可刷；不改变 Passport 的受保护 Flash 区域。
- 不将个人硬件版本描述成具备量产级密钥防提取能力的产品。

### 1.3 最终交付物

每仓须包含可构建源码、固定依赖、用户安装说明、脱敏 API fixtures、Host tests、真机验收记录、Release 产物及校验和。交付报告分别列出 `Build / Host tests / CI / Device tests / Installer tests / Unverified`，禁止用编译成功替代联网、安装或电源验收。

## 2. 上游来源、版本与硬件边界

### 2.1 核对过的源代码基线

以下 SHA 是本文核对基线，开发开始后写入各仓 `docs/upstream-lock.md`。升级须重新检查对应兼容约束，不使用浮动 `main` 作为发布依赖。

| 上游 | 核对提交 | 用途 |
|---|---|---|
| [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport/tree/f913af29a387f8983ef4faa9f1580b14e88b0740) | `f913af29a387f8983ef4faa9f1580b14e88b0740` | Passport BSP、LVGL、分区、Recovery 与安装验证 |
| [itopinion/zectrix-note4-epd-demo](https://github.com/itopinion/zectrix-note4-epd-demo/tree/ca285c98ed0641f86780edb1f5ec77b0335fe649) | `ca285c98ed0641f86780edb1f5ec77b0335fe649` | NOTE4 board adapter、SSD2683 驱动、按钮与 RTC |
| [FoloToy/folo-ai-passport-xiaozhi](https://github.com/FoloToy/folo-ai-passport-xiaozhi/tree/d24fce080d86d7cc642f71585f6efde40fb99104) | `d24fce080d86d7cc642f71585f6efde40fb99104` | 同板 Wi-Fi 热点配网参考；不整体移植其应用或分区 |
| [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect/tree/347682fa013b52f863052ad4b1a793ca3cabff17) | `347682fa013b52f863052ad4b1a793ca3cabff17`，组件声明 `3.2.2` | 两端首选 Wi-Fi/Portal 组件 |
| [quellogs/quellog-eink](https://github.com/quellogs/quellog-eink/tree/19987784877eb086e01089797528e31615e5e982) | `19987784877eb086e01089797528e31615e5e982` | NOTE4 UX、EPD 配网提示、`quellog_wifi` 备选 |
| [vibe-cafe/vibe-usage-app](https://github.com/vibe-cafe/vibe-usage-app/tree/34d50754e0504b4a33b87d1f7927d462f30cb98e) | `34d50754e0504b4a33b87d1f7927d462f30cb98e` | Device Flow、请求字段、客户端响应模型 |
| [vibe-cafe/vibe-usage](https://github.com/vibe-cafe/vibe-usage/tree/8f8d88fd70612b3853363eb0bd2ff3ba6ae2ef79) | `8f8d88fd70612b3853363eb0bd2ff3ba6ae2ef79` | 上传数据及 Token 字段语义 |

社区项目发现入口：[ZECTRIX 社区开源项目](https://wiki.zectrix.com/zh/software/Community-OpenSource)。实现以对应源码为准，不能仅凭社区介绍推断功能已通过本项目验收。

### 2.2 两个固件的定位

| 项目 | vibe-usage-ai-passport | vibe-usage-zectrix |
|---|---|---|
| 产品形态 | 桌面/随身交互式 Usage Monitor | 低功耗、画面常留的 Usage Dashboard |
| MCU | ESP32-C3 | ESP32-S3 |
| Flash | 8 MB，固定保护区 | 上游默认 16 MB，实机确认 |
| PSRAM | 无，所有关键资源竞争内部 RAM | 上游配置 Octal PSRAM；容量由真机检测记录 |
| 屏幕 | ST7789P3，240×320，RGB565 | SSD2683，400×300，4.2 英寸黑白 EPD |
| UI | LVGL，经 BSP 操作显示和按钮 | `zectrix_epd` + 原生 1bpp canvas |
| 常规刷新周期 | 15 分钟 | 30 分钟 |
| 默认初版模式 | USB 常开、30 秒无操作降背光 | 常开完成闭环，随后启用验收通过的 deep sleep |
| 主要工程门槛 | TLS 峰值内存、3 MiB app、安装兼容 | full/partial 生命周期、休眠唤醒、电池 latch |

Passport 的现有 Wi-Fi Demo 只承担扫描演示；NOTE4 Demo 的 Wi-Fi RF 测试也不等于用户配网。两仓均须集成配网组件。FoloToy 小智项目 README 记录了基础启动/配网验证，其依赖声明使用 `78/esp-wifi-connect: ~3.2.2`，这证明有同板参考，不能替代本固件的内存和稳定性测试。[同板项目说明](https://github.com/FoloToy/folo-ai-passport-xiaozhi/blob/d24fce080d86d7cc642f71585f6efde40fb99104/README.md)

## 3. 已冻结的架构决策

| 编号 | 决策 |
|---|---|
| D01 | 两仓直接连接 Vibe 服务，不保留 GitHub Pages 中转链路 |
| D02 | 两端默认 SoftAP + Captive Portal，首选 `78/esp-wifi-connect` |
| D03 | Wi-Fi adapter 只包装组件生命周期与产品行为，不重写 DNS、DHCP、扫描、凭据存储栈 |
| D04 | `vibe_usage` 不依赖 LVGL、EPD、具体 GPIO；UI 不接触原始 JSON、Key 和 NVS |
| D05 | Today 为所选时区的当日；7D 为今日及前 6 个自然日，不是过去 168 小时 |
| D06 | 日常取单日，完整响应校验后按日替换；禁止累加两次请求的 totals |
| D07 | 日缓存、授权记录、设置都有版本及归属信息；错误响应不能覆盖 LKG |
| D08 | Passport 从官方 baseline 衍生并保留 Recovery 合同；NOTE4 只使用黑白 UI |
| D09 | P0 不改变 Secure Boot、Flash Encryption 或 eFuse；安全强化单独验证安装链 |
| D10 | 先在 C3 跑稳核心，再移植 NOTE4；满足第 21 节条件后抽取独立公共组件 |

## 4. 统一架构与组件责任

```text
用户电脑上的 Vibe Usage CLI/App ──已有采集与同步──> VibeCafe
                                                    │
                           Device Flow + GET /api/usage（HTTPS）
                                                    │
                     ┌──────────────────────────────┴──────┐
                     │ components/vibe_usage                │
                     │ auth / client / parser / aggregate   │
                     │ daily cache / store / error mapping  │
                     └──────────────────┬───────────────────┘
                                        │ immutable UsageSnapshot
                     ┌──────────────────┴───────────────────┐
                     │ app_controller + single network worker│
                     └──────────┬────────────────┬──────────┘
                                │                │
                       LVGL + Passport BSP   1bpp UI + NOTE4 board

app_controller ──> wifi_adapter ──> 78/esp-wifi-connect
app_controller ──> time_adapter / power_adapter / storage port
```

| 模块 | 输入/输出与责任 |
|---|---|
| `vibe_auth` | 发起 code、轮询、识别授权错误；凭据只返回到受控业务 worker |
| `vibe_client` / `vibe_http` | URL 构造、TLS、请求限额、取消、HTTP 错误分类 |
| `vibe_parser` | 增量消费 HTTP 字节，验证 schema，输出当前单日候选聚合 |
| `vibe_aggregate` | Token 安全求和、source 归并、Top N/Other、占比计算 |
| `vibe_cache` | 日槽替换、7D 完整性、滚动、reconciliation 调度及进度 |
| `vibe_store` | NVS 编解码、版本、CRC、提交、账号/时区隔离 |
| `wifi_adapter` | 把第三方 C++ 组件转为统一事件和 C 接口，管理配置模式 |
| `time_adapter` | UTC、时区、本地日期、单调时钟；NOTE4 额外读写 RTC |
| `app_controller` | 唯一业务状态拥有者，串行处理事件，调度工作与电源 |
| `ui_*` | 只消费快照和 UI 状态，发送用户 intent |
| `power_*` | 板级外设停机、亮度、休眠、唤醒、关机；不嵌入公共 core |

### 4.1 并发与资源所有权

- 每设备最多一个云 HTTP 请求；Device Flow 和 Usage fetch 不同时进行。
- Button/Wi-Fi/定时器 callback 只向队列投递小事件，不读写 Flash、不发 HTTP、不做屏幕刷新。
- `network_worker` 串行执行阻塞网络及解析。UI 使用快照副本；不共享可变聚合数组。
- Passport 在 UI task 中操作 LVGL；遵循 BSP 的 LVGL 锁约定。NOTE4 使用独立显示 worker，因为 EPD API 为同步阻塞调用。
- 切换页面前撤销相关订阅；unlink/reset/cancel 增加 `generation`。旧 generation 的网络结果一律丢弃，不能恢复已解绑账号的数据。
- NVS、默认 event loop、`esp_netif`、Wi-Fi driver 各只有一个初始化拥有者；删除 Demo 初始化入口，避免双重初始化。

## 5. 公共 `vibe_usage` 数据模型与 API

以下为拟实现的公共契约，不是已存在库。公共头文件使用 C ABI，C++ 调用端使用 `extern "C"`；纯 parser/aggregate/cache 逻辑不得依赖 ESP-IDF。

```c
#define VIBE_MAX_AGENTS       12   /* UI: 最多 11 个 source + Other */
#define VIBE_CACHE_DAYS        8
#define VIBE_SOURCE_SLOTS     24   /* 内部: 23 个 source + overflow Other */
#define VIBE_SOURCE_ID_BYTES  48   /* 含结尾 NUL；超限归 Other，禁止截断碰撞 */

typedef enum {
    VIBE_READY,
    VIBE_EMPTY,
    VIBE_STALE,
    VIBE_AUTH_REQUIRED,
    VIBE_LINK_REQUIRED
} vibe_data_state_t;

typedef struct {
    char id[VIBE_SOURCE_ID_BYTES];
    uint64_t today_tokens;
    uint64_t seven_day_tokens;
    uint16_t today_bp;             /* 0..10000，万分比 */
    uint16_t seven_day_bp;
} vibe_agent_usage_t;

typedef struct {
    uint32_t schema_version;
    uint32_t generation;
    int32_t today_key;             /* YYYYMMDD，仅作标签，不做加减运算 */
    uint64_t today_tokens;
    uint64_t seven_day_tokens;
    vibe_agent_usage_t agents[VIBE_MAX_AGENTS];
    uint8_t agent_count;
    uint8_t valid_days_mask;       /* bit 0=今日，bit 6=今日-6 */
    bool today_complete;
    bool seven_day_complete;
    bool has_any_data;             /* API 字段原义，不用它推导每日完整性 */
    bool has_lkg;
    bool time_valid;
    bool persisted;
    bool sources_collapsed;
    int64_t last_fetch_at;         /* 今日最近完整成功请求的 UTC 秒 */
    int64_t last_reconcile_at;     /* 最近一次完整 7D 对账的 UTC 秒 */
    vibe_data_state_t state;
} vibe_usage_snapshot_t;
```

内部每日日槽存：`date_key`、UTC 成功时间、`valid`、`total_tokens`、24 个 source 计数、响应 `hasAnyData`。同一 cache blob 存 source 字典、账号 generation、时区配置版本、`metric_id`、有效范围和对账进度。不要把上面的 C struct 原样 `memcpy` 为磁盘格式；使用固定宽度、明确字节序的编码。

### 5.1 公共入口

```c
esp_err_t vibe_init(const vibe_config_t *config);
esp_err_t vibe_request_device_code(vibe_device_code_t *result);
esp_err_t vibe_poll_device_code(const char *device_code,
                               vibe_auth_result_t *result);
esp_err_t vibe_fetch_today(vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_reconcile(vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_load_cached(vibe_usage_snapshot_t *snapshot);
esp_err_t vibe_unlink(void);
esp_err_t vibe_factory_reset(void);
void vibe_cancel(void);
```

- `vibe_config_t` 包含 HTTPS bootstrap origin、时区配置、refresh 秒数、各资源上限、clock/store/transport ports；不通过 UI 传入 Key。
- `vibe_device_code_t` 包含 code 响应字段、单调过期时间；`vibe_auth_result_t` 包含成功/等待/拒绝/过期/已交付等状态。Key 字段仅 worker 可访问，不复制到 UI 事件。
- UI 通过 `app_dispatch(REFRESH/RELINK/RECONFIGURE_WIFI/RESET)` 发意图，不能直接调用阻塞 API。
- `vibe_fetch_today` 最多执行一个完整单日请求；`vibe_reconcile` 每次最多执行一个到期单日任务，返回剩余进度，由 controller 再调度，确保可取消与预算可控。
- `vibe_unlink` 删除 Vibe 凭据与该账号缓存，保留 Wi-Fi、时区和设备名。`vibe_factory_reset` 只重置 Vibe 自己的命名空间；整机应用重置由 controller 继续调用 `wifi_adapter_clear_credentials()`，完成后重启。
- 除 `esp_err_t` 外，内部保留 `HTTP_STATUS / DNS / TLS / TIME_INVALID / BODY_TOO_LARGE / SCHEMA / OVERFLOW / STORAGE / CANCELLED` 等明确错误类别；不能全部映射成“无数据”。

### 5.2 数量、来源和占比

P0 固定 `metric_id = API_TOTAL_V1`：**聚合服务端 `bucket.totalTokens`，不再叠加其他 token 字段。** `source` 按服务端字符串区分，显示名由本地小字典映射，如 `codex`、`claude-code`；不按营销名称猜测合并源。

这里存在必须保留的口径边界：本次核对的 CLI 用 `input + output + reasoning` 生成 `totalTokens`；macOS App 的 `computedTotal` 另加 `cachedInputTokens`。因此本版不能声称与 App 主数字必然相等。P0 对账同时记录原始 total、各细分字段与 App 值，验收参照为 `/api/usage` 的原始 total 求和。若后续产品要求改为 App 口径，须升级 `metric_id`、重新拉取缓存并修改验收数据，禁止静默切换。[CLI 聚合实现](https://github.com/vibe-cafe/vibe-usage/blob/8f8d88fd70612b3853363eb0bd2ff3ba6ae2ef79/src/parsers/aggregate.js)；[App 数量模型](https://github.com/vibe-cafe/vibe-usage-app/blob/34d50754e0504b4a33b87d1f7927d462f30cb98e/VibeUsage/Models/UsageBucket.swift)

- 每个合法 bucket 计入一次；不同 project/model/hostname 下的 bucket 都参与聚合，不能只按 `source + bucketStart` 去重，否则会漏数。
- 不自行修复上游重复上传；若 API 存在重复 bucket，先确认服务端唯一性契约。
- 计数用 `uint64_t`；解析负数、非整数或溢出均使该日候选失败。求和先检查 `UINT64_MAX - sum >= value`。
- UI 按当前时间窗口 Token 降序，平手按 source id 排序；只显示前 11 个，其余加为 Other。内部 source 槽超限也归 Other，并设置 `sources_collapsed`，保证总量不丢失。
- 全部 source 数量含 Other 的和必须等于总量；0 总量占比全为 0。占比采用避免 `tokens * 10000` 溢出的商余算法；显示整数百分比可用最大余数法使总和为 100%。
- K/M/B 缩写只发生在 UI；测试、持久化和对账全部使用整数原值。

## 6. Vibe Usage Device Flow 授权

客户端契约来源：[APIClient.swift](https://github.com/vibe-cafe/vibe-usage-app/blob/34d50754e0504b4a33b87d1f7927d462f30cb98e/VibeUsage/Services/APIClient.swift)。这是公开客户端实现，不是完整服务端 OpenAPI；异常响应仍须补脱敏 fixtures。

### 6.1 发起授权

在 Wi-Fi 获取 IP 且系统时间可用于证书验证后：

```http
POST https://vibecafe.ai/api/usage/device/code
Content-Type: application/json

{"clientName":"Vibe Usage AI Passport","hostname":"VibePassport-A82C"}
```

NOTE4 的 `clientName` 为 `Vibe Usage NOTE4`，hostname 为设备本地生成的非个人名称。不要上报电脑 hostname、完整 MAC 或用户身份信息。

成功响应必须校验：

| 字段 | 类型与用途 |
|---|---|
| `deviceCode` | 非空字符串，仅 RAM，供轮询，不上屏不入日志 |
| `userCode` | 非空字符串，屏幕显示，与手机页面核对 |
| `verificationUri` | HTTPS 地址，用于手工授权入口 |
| `verificationUriComplete` | HTTPS 地址，优先生成授权 QR |
| `expiresIn` | 正整数秒；创建本地单调截止时间 |
| `interval` | 正整数秒；不得比服务端要求更频繁轮询 |

字符串必须有上限：code/key 512 bytes、userCode 64 bytes、URL 1024 bytes（均含 NUL）。超限报协议错误，不截断后继续。Auth 响应总大小上限 8 KiB。

### 6.2 轮询

```http
POST https://vibecafe.ai/api/usage/device/poll
Content-Type: application/json

{"deviceCode":"<仅当前授权会话内存中的值>"}
```

| 响应 | 行为 |
|---|---|
| 200 + `apiKey`，无冲突 error | 校验 Key 和 `apiUrl`，立即原子持久化，再进入首次 Usage 同步 |
| 200 + `error=authorization_pending` | 至少等待 `interval` 后继续 |
| 200 + `error=access_denied` | 清理临时 code，`LINK_REQUIRED`；等待用户重新发起 |
| 200 + `error=expired_token` | 清理临时 code，`LINK_REQUIRED`；旧 QR 失效 |
| HTTP 410 | 客户端注释为已交付、不重放；当前设备没持有已保存 Key 时只能重新开始授权 |
| `slow_down` | 若服务端实际返回，增加间隔至少 5 秒，记录 fixture；不能加快重试 |
| 429 | 尊重合法 `Retry-After`，仍受授权截止时间约束 |
| 超时/5xx | 延迟重试，间隔不小于原 interval；达到截止时间结束 |
| 空对象、未知 error、矛盾字段、坏 JSON | 显示协议错误，不当作授权成功或永久 pending |

公开 App 接受 200 和 410 的 poll JSON；不能直接照通用 OAuth 库假设所有 pending 都是 HTTP 400。轮询 schedule 使用单调时钟，避免 SNTP 校时导致无限延长；重启不恢复未完成 code，重新申请即可。

### 6.3 URL 和凭据规则

- 默认 bootstrap origin 为 `https://vibecafe.ai`。成功 `apiUrl` 非空时必须验证并保存；缺省时回退本次可信 bootstrap origin，与公开 App 行为保持一致。
- P0 使用固件内的可信 origin allowlist，默认仅 bootstrap；未来增加服务器时通过配置版本扩展。不能把 Key 发往未经校验的返回域名。
- `apiUrl` 只允许 HTTPS origin，不含用户名、密码、query、fragment；末尾 `/` 规范化。请求路径统一追加 `/api/usage`，禁止重复 `/api`。
- 授权二维码只含验证 URL，不含 API Key、deviceCode；QR 以整数像素缩放并保留四模块 quiet zone。二维码放不下时显示较短的 `verificationUri` QR 和 userCode，不裁切。
- 授权等待时保持网络与屏幕必要显示；NOTE4 不每秒重绘倒计时。取消或超时后释放 code 和临时 HTTP buffer。
- Key 持久化失败不能显示“绑定成功”。成功 poll 可能不可再次领取：先存储再做其他网络动作，失败时提示重新授权。

## 7. 直接调用 `/api/usage` 与契约验证

```http
GET {api_origin}/api/usage?days=1&tz=Asia%2FShanghai
Authorization: Bearer <NVS 中的 apiKey>
Accept: application/json
Accept-Encoding: identity
```

公开客户端使用 `days`、`from`、`from/to`、`tz`。其 custom range 把 from/to 格式化为 `YYYY-MM-DD`，单独 from 则使用 ISO 8601。**客户端代码不能证明服务端端点是 inclusive 还是 exclusive，也不能证明 `days=1` 等于所选时区的自然日。** [请求构造依据](https://github.com/vibe-cafe/vibe-usage-app/blob/34d50754e0504b4a33b87d1f7927d462f30cb98e/VibeUsage/Services/APIClient.swift)

### 7.1 开发前必须落盘的契约

在 `docs/api-contract.md` 和 `tests/fixtures/contract-meta.json` 记录：服务 origin、核对日期、客户端 SHA、请求参数、HTTP 状态、脱敏响应结构、日期边界、是否分页/截断、`hasAnyData` 的全账号或当前范围含义、`bucketStart` 格式、metric_id。

P0 使用测试账号或经用户授权的只读 API 请求完成以下核对，保存合成或去标识 fixtures，不把真实 Key、project、hostname 放进仓库：

1. 分别请求同日 `from=D&to=D`、相邻日 `from=D&to=D+1` 和 `days=1`，对照午夜附近的已知 buckets，确定日期包含关系。
2. 确认所有返回 buckets 均可解析为带时区的绝对时刻，并映射到请求日。
3. 确认 API 是否提供 pagination/truncation 元信息；有分页时遍历全部页才算单日成功。没有文档或证据时不能宣称响应一定完整。
4. 对比空账号、账号有历史但今日无记录、今日有记录三种 `hasAnyData`/buckets 组合。
5. 比较原始 `totalTokens` 求和与 App 的 computedTotal，记录差异，按第 5.2 节固定口径验收。

`vibe_build_daily_query(D)` 只实现已验证的一种范围模式：若 to 包含，使用 D/D；若 to 排除，使用 D/nextCalendarDay(D)。两仓共享 fixture 和期望结果。上述行为未确认时允许继续使用本地 mock 开发，真实 7D 正确性不得标 PASS，不能通过拍脑袋选择边界来消除阻塞。

### 7.2 处理字段

P0 只保留 `buckets[].source`、`bucketStart`、`totalTokens` 和根级 `hasAnyData`。`sessions`、project、hostname、model、费用等只做有界语法跳过，不保存。未知字段兼容跳过；必需字段缺失、重复、类型错误使该日请求失败。

服务端已按多个维度划分 bucket。固件做的是跨维度求和，不再实现日志级去重，不组合相互重叠日期请求的原始桶。

## 8. SoftAP + Captive Portal 配网

### 8.1 依赖选择与适配边界

两仓初始固定 `78/esp-wifi-connect` 3.2.2，并提交 `dependencies.lock`。发布时锁定实际组件内容哈希；Git 快照与 Registry 包内容如有差异，以实际编译包重跑验证。包装其 `WifiManager`、`SsidManager`，对上层暴露：

```text
wifi_adapter_init
wifi_adapter_has_credentials
wifi_adapter_start_station / stop_station
wifi_adapter_start_provisioning / stop_provisioning
wifi_adapter_list_credentials / remove_credential / clear_credentials
events: SCANNING, CONNECTING, GOT_IP, DISCONNECTED, PORTAL_ENTER, PORTAL_EXIT
```

不要把“网络已关联”“取得 IP”“时间有效”“Vibe 可用”合并为一个 Connected。业务请求只在所需前提全部满足后发起。

当前首选组件的真实行为：`wifi` namespace、最多 10 组 SSID、门户为 `http://192.168.4.1`、AP 默认开放、名称默认附加 MAC 最后两字节；表单会测试连接后保存。[组件说明](https://github.com/78/esp-wifi-connect/blob/347682fa013b52f863052ad4b1a793ca3cabff17/README.md)；[Portal 实现](https://github.com/78/esp-wifi-connect/blob/347682fa013b52f863052ad4b1a793ca3cabff17/wifi_configuration_ap.cc)

本产品做必要的小范围集成：

- 产品热点命名为 `VibePassport-XXXX` / `VibeNote4-XXXX`；XXXX 为第一次启动生成并保存的随机标识。现成 API 只有 prefix 时，通过带来源说明的最小补丁增加完整 SSID 配置，不能声称原库已支持随机后缀。
- 复用现成页面与 DNS；隐藏 OTA URL、sleep 等与产品无关字段，并关闭相应写入 handler，不能只藏前端控件。
- P0 个人版本保留组件开放热点行为，限物理在场使用、10 分钟超时；页面说明配网窗口。此链路的 SSID/密码经本地 HTTP 传送，开放 AP 不具备链路保密性，须在用户文档明确。P1 增加每次配网随机 WPA2 密码及 Wi-Fi QR，需要组件最小补丁并重新测手机兼容，不能把此能力标为默认已有。
- 审查组件日志，禁用 SmartConfig/BluFi 及其凭据日志路径；不复制 README 中遇 NVS 错误就整区擦除的示例。
- iOS/Android 自动弹出失败时，屏幕始终提供热点名和 `192.168.4.1`；无需下载 App。

### 8.2 首次启动流程

```text
启动 → 加载本地配置与 LKG → 无 Wi-Fi 凭据
  → 显示 Wi-Fi Setup、热点名、Wi-Fi QR/网址
  → 启动 SoftAP/Portal
  → 手机连接热点 → 扫描或手工填写 SSID → 填密码 → Connect
  → 组件测试 STA 连接并取得 IP → 保存凭据
  → Portal 显示成功 → 关闭 HTTP/DNS/AP、释放相关内存
  → 正常 STA → 校时 → 获取 Vibe Device Code
  → 显示授权 QR + userCode → 手机登录并确认
  → poll 成功 → 保存 apiKey/apiUrl → 抓 Today → 展示
  → 后台逐日补齐前 6 日 → 完整 7D
```

Wi-Fi QR 与 Vibe 授权 QR 必须是两个不同页面。关闭 AP 后提示手机恢复家庭 Wi-Fi/蜂窝网络，避免手机仍连着无互联网热点而打不开 Vibe 页面。

密码错误、目标 AP 不可达或测试超时：留在配网页并显示可重试原因，不删除之前有效的其他网络。取得 IP 但外网不可用：保存有效 Wi-Fi，显示互联网/校时失败；不能误报为密码错误，也不反复抹掉重配。

### 8.3 重连、重新配网与重置

| 操作/触发 | 规定行为 |
|---|---|
| 有凭据启动 | 尝试保存的 Wi-Fi；利用组件多 SSID 选择及退避 |
| 正常联网失败 | 保留 LKG；USB 模式后台重试，NOTE4 电池模式按唤醒预算退回休眠；不自动长期开放热点 |
| Settings → Wi-Fi → Reconfigure | 用户明确进入 10 分钟配网窗口；保留 Vibe Key 与缓存 |
| 启动时按住 OK 2 秒 | 两设备进入重新配网；不得覆盖 Passport 的 UP 5 秒 Recovery 或 NOTE4 DOWN 3 秒关机 |
| 删除一组 Wi-Fi | adapter 调用组件管理 API；删除最后一组后进入 WIFI_REQUIRED，由用户开始配网 |
| 配网取消/超时 | 停 AP/DNS/HTTP；有旧网络则恢复 STA，无则显示 WIFI_REQUIRED；按 OK 可重开 |
| Vibe Reconnect | 只重做 Device Flow，不要求重新输入 Wi-Fi |
| Vibe Unlink | 删除 Vibe 凭据、账号缓存；Wi-Fi/时区保留 |
| Reset Vibe settings | 本机二次确认后清应用设置、Wi-Fi、Vibe 凭据与缓存；不擦 Passport cardid/Recovery |

### 8.4 NOTE4 fallback 条件

默认先使用同一组件。只有记录了可复现的 NOTE4 阻塞问题（例如 5.5.3 下配网崩溃、退出后无法关无线电、持续内存泄漏），并完成一次有界修复尝试仍不通过，才切换 `quellog_wifi`。记录 reproduction、组件版本、日志和取舍到 ADR。

fallback 范围仅 `firmware/components/quellog_wifi`、必要静态页面及依赖；不移入 Quellog 服务、记账模型和整个应用。其 namespace 同为 `wifi`、容量为 5，接口如 `/credentials` 与首选组件 `/saved/list` 不同，统一由 adapter 屏蔽。[备选组件](https://github.com/quellogs/quellog-eink/tree/19987784877eb086e01089797528e31615e5e982/firmware/components/quellog_wifi)

初次刷入 fallback 构建时如需迁移，必须基于实际格式显式导入最多 5 组有效凭据并告知容量；禁止只因 namespace 相同就假定所有键与行为完全兼容。保留许可证和来源提交，重跑整套配网验收。

## 9. 时间、Today/7D 与 daily rolling cache

### 9.1 时间系统

- 系统时间、成功时间戳和 RTC 存 UTC；API 查询与本地分日使用同一用户时区。
- 默认 `Asia/Shanghai`。P0 设置页可选 `Asia/Shanghai` 和 `UTC`，分别映射 POSIX `CST-8`、`UTC0`；P1 扩展经过 DST 测试的 IANA→POSIX 表。不得将任意 IANA 名字直接塞给 libc `TZ` 并假定完整 tzdb 存在。
- Portal 不必新增时区 HTTP 接口；P0 在设备 Settings → Timezone 设置，后续可复用组件扩展页面。更改时区后取消在途任务、更新配置版本、失效旧分日缓存并重抓 7D。
- 开机使用可验证的 RTC 时间，联网后以 SNTP 校准；无有效时间则显示“正在校时”，不要用 1970 日期请求 Usage，不关闭 TLS 日期验证。
- `last_fetch_at` 是设备成功获取数据的时间，不是上游 Collector 最近上传时间。API 无上游更新时间时，不显示虚构的“实时采集”。
- 超时、重试、授权截止与按钮长按使用单调时钟。日期滚动用公历运算，禁止 `YYYYMMDD + 1` 或固定减 `86400` 推导任意时区的下一自然日。
- 用户改时区或校时导致日期回跳时，重新计算窗口并标记对账到期；以日期任务标识防止重复对账，不计算负数“多久前”。

ESP-IDF 的系统时间及 POSIX 时区机制见[官方 System Time 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/system_time.html)。

### 9.2 缓存范围及完整性

缓存保留 `[today-7, today]` 共 8 个自然日；屏幕 7D 只汇总 `[today-6, today]`。第 8 槽用于滚动和故障恢复，不计入 7D。

每个日槽有独立 `valid` 和成功时间：

- **valid zero**：该日完整请求成功且 bucket 数或合计为零，可以显示 0。
- **missing**：该日从未成功获取，不得作为 0 加入完整总量。
- **old valid**：曾成功但已到更新时限，可继续显示并标记过期。

初次同步先取 Today，立即显示可信的今日结果；依次取 yesterday 至 today-6。补齐前显示 `7D syncing 3/7` 或 `7D incomplete`，不能把部分和标成完整 7D。如展示部分和，必须同时显示覆盖天数。

跨午夜时：新的 Today 没成功读取前显示 `— / Updating`；昨天的数字只能带明确日期作为历史 LKG 展示，不能换上 Today 标签。离线超过 7 天时旧快照仍可作为“截至 YYYY-MM-DD 的历史”查看，但不能算进当前窗口。

### 9.3 单日事务算法

```text
fetch_day(D):
  capture account_generation, config_revision, metric_id
  create empty candidate for D
  request verified daily range; incrementally parse all pages if applicable
  for each bucket:
    validate fields and unsigned integer
    parse bucketStart as absolute time; convert to configured calendar day
    require it belongs to the verified request range
    aggregate into candidate with checked addition
  require HTTP 200 + complete HTTP body + complete JSON + expected schema
  require no truncation/pagination remains + valid hasAnyData semantics
  require generation/config/metric unchanged and operation not cancelled
  replace cache[D] with candidate; never add candidate onto old cache[D]
  serialize a new cache generation; atomically commit and read back
  publish a new immutable snapshot
```

服务端修订导致某日从 1000 降为 700 时，结果必须为 700；不能取 max，也不能保留旧 total。半包、超限、解析失败、取消均丢弃 candidate，原日槽和其成功时间不变。

一个日请求发现落在目标日以外的 bucket 时，必须与第 7 节已确认的端点行为一致；范围契约声称严格单日却返回其他日，视为契约异常而不是悄悄过滤后宣称完整。

### 9.4 日常刷新及 daily reconciliation

| 场景 | 请求计划 |
|---|---|
| 常规自动刷新 | Passport 15 分钟、NOTE4 30 分钟，只取 Today |
| 首次绑定/缓存无效/时区或 metric 改变 | Today 优先，逐日补齐最近 7 日 |
| 正常午夜滚动 | 先刷新 Today；昨天标记为需要再次确认，完整对账按当日计划执行 |
| 每日例行对账 | 按设备稳定随机偏移选择 03:00–03:59，当天该时刻后首次在线执行最近 7 日 |
| 错过对账窗口 | 下一次联网补做，不等待次日；同日已完成则不重复 |
| 离线跨日且距最近成功同步 ≥24 小时 | 联网后立即补做完整 7D，并记作当天对账完成 |
| 用户 Refresh now | 只刷新 Today；若窗口缺日，随后补缺日 |
| 用户 Reconcile 7D | 显式排队最近 7 日，遵守请求间隔和资源预算 |

同一轮对账把 Today 请求复用为日常刷新，全部串行。正常无错误时每日约增加 6–7 个单日请求，而非每次拉整周。完成某一天即可原子替换该日；只在本轮 7 个日期都成功后更新 `last_reconcile_at` 和“今日对账完成”标记。中途失败保留进度和旧日槽，下次仅补剩余任务；午夜跨越造成窗口改变时重新计算所需日期，不能拿旧窗口标记完成。

单次后台工作预算：USB 常开每轮最多 3 分钟；NOTE4 电池普通唤醒最多 60 秒，对账唤醒最多 120 秒、最多取 2 日。余下任务在后续 30 分钟唤醒继续，避免为了补历史整晚不睡。首次手机配网与授权是独立交互窗口，不受 60 秒普通唤醒限制。

历史日缓存不表示永远不会变。7D 对账仅修正可见窗口，超过窗口的迟到数据不在本版展示范围。

## 10. NVS 数据模型、持久化与恢复

### 10.1 namespace 与键

ESP-IDF 的 NVS namespace 和 key 最长 15 字符。下列键均符合上限；原讨论中的 `refresh_interval` 等长名称不能直接照搬。[NVS 官方限制](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/nvs_flash.html)

| namespace | key | 内容与拥有者 |
|---|---|---|
| `wifi` | 由所选库维护 | SSID/password 与该库支持的配置；只经 wifi adapter 访问 |
| `vibe_cfg` | `settings_v1` | schema、设备随机 ID/名称、时区、刷新周期、UI 模式、config revision |
| `vibe_auth` | `auth_v1` | 单一 blob：schema、account generation、api origin、apiKey 或 tombstone |
| `vibe_cache` | `cache_a` | 版本化聚合快照 A |
| `vibe_cache` | `cache_b` | 版本化聚合快照 B |
| `vibe_meta` | `reset_v1` | 应用重置进度标记，用于掉电后继续完成清理 |
| Passport 受保护分区 | `cardid` | 官方 identity，任何上述模块都不得读写或清空 |

缓存 blob 包括：magic、schema、单调递增 sequence、payload length、CRC32、account generation、config revision、metric_id、时区、source 字典、8 日日槽、对账进度及时间。LKG 由这些数据派生，不另外持久化一份带原始响应的大 JSON。

### 10.2 空间与写入策略

- Passport 上游 NVS 仅 `0x6000`（24 KiB），NOTE4 Demo 也为此默认值。先在此容量实现，不能为了缓存随意改分区。
- 单个 cache blob 上限 4096 bytes；双槽合计不超过 8192 bytes。24 个 source 槽和 8 日的定长计数应按紧凑编码控制在此预算；构建及测试检查真实最大序列化长度。
- `settings_v1` 上限 512 bytes，`auth_v1` 上限 2048 bytes。Flash 限额须覆盖最长 Key/URL、10 组 Wi-Fi、NVS 条目开销及 GC 空闲页，不能只按 payload bytes 相加。
- 每次完整 Usage 成功可提交一次；最短持久化间隔 60 秒，连续按键刷新合并。未发生数据或对账进度变化时只在成功时间前移满 1 小时后提交 freshness checkpoint。
- NOTE4 即将 deep sleep 时，成功时间和待对账进度若有变化，提交一次 checkpoint；失败请求不反复写失败时间。用户 unlink/reset/设置改变立即提交。
- 测量 24 KiB NVS 在最长凭据、双 cache、反复更新及掉电情况下能否 GC。空间不足先缩小编码/移除非必要字段；若必须另加数据分区，属于单独兼容变更，须重跑 Passport 完整安装合同。

### 10.3 原子性规则

1. cache 更新写 sequence 较旧的槽，`nvs_commit` 后读回校验；启动时选同账号、时区、metric 下 CRC 正确且 sequence 最大的槽。
2. 不依赖多个 NVS key 组成事务；某次响应的数据与成功时间必须在同一个 cache blob 内。
3. auth 使用单一 `auth_v1` blob 原子替换，包含 generation。unlink/401 先写入更高 generation 的无 Key tombstone，再清 cache；这样掉电不能让旧缓存或旧请求恢复授权。
4. 普通换 Wi-Fi不变更 account generation。重新授权无论是否同一账号都创建新 generation，旧账号数据不能混入新账号。
5. 双 cache 都损坏时只丢弃 cache 并重新同步；auth 或 settings 损坏时进入对应配置/授权流程，不做整 Flash 擦除。
6. 用户应用重置先存 `reset_v1` 标记，再 tombstone auth、经库清 Wi-Fi、删除 Vibe cache/config，最后清 reset 标记并重启。开机发现标记要继续完成重置，不先联网。
7. NVS 写失败时保留旧持久化数据。本次成功候选可暂存 RAM 并显示“未保存”；`persisted=false`，不能报告重启恢复通过。
8. `ESP_ERR_NVS_NO_FREE_PAGES` / version 不兼容进入存储故障处理。禁止自动 `nvs_flash_erase()`；禁止将 demo 的通用全擦恢复代码带入 Passport。

NVS 删除属于逻辑删除，不能承诺攻击者无法从 Flash 历史页恢复明文密钥。用户丢失/转让设备时，还应在服务端撤销对应 Key；当前固件没有可假定存在的云端 revoke 接口。

## 11. HTTP、TLS 与受限 JSON 解析

### 11.1 HTTP/TLS 基线

使用 `esp_http_client + mbedTLS + ESP certificate bundle`，严格校验证书链、hostname 和有效期。禁止 insecure 模式、跳过 CN/hostname、硬编码当前叶证书。HTTP streaming 能力参考[ESP HTTP Client 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/protocols/esp_http_client.html)。

| 参数 | P0 默认 |
|---|---|
| 同时在途云请求 | 1 |
| Auth 请求 deadline | 15 秒 |
| 单日 Usage 请求 deadline | 30 秒，独立于 read timeout |
| HTTP read 空闲 timeout | 10 秒 |
| 手动刷新最小间隔 | 60 秒，重复 intent 合并 |
| Usage 自动失败退避 | 60、120、240 秒，上限 30 分钟，±20% jitter |
| 429 | `Retry-After` 优先，不早于服务端允许时间；跨休眠保存下次可请求时间 |
| 启动 SNTP 等待 | 最多 15 秒；失败进入 TIME_REQUIRED/STALE，而非关闭 TLS 校验 |
| HTTP body | 支持 Content-Length、chunked、未知长度；读完后检查完整性 |
| Redirect | P0 关闭自动跳转；跨 origin 绝不携带 Authorization |
| 压缩 | 请求 identity；未实现有界解压时拒绝非 identity response |

403 先显示权限/服务配置错误，保留 Key 等待用户处理，不自动当成 401 擦除。404/400 显示 API 配置或范围契约错误，停止紧密重试。5xx、DNS、TLS、断线均保留 LKG；TLS 失败绝不能退回 HTTP。

### 11.2 Streaming 路径：正式版本默认

```text
HTTP 2 KiB read buffer
  → 增量 JSON tokenizer（跨 chunk 状态）
  → 识别根字段与 buckets[]
  → 单个 bucket 临时字段
  → 单日 candidate 聚合
  → 完整响应结束校验
  → 持久化并发布
```

实现要求：

- Parser buffer 不依赖整个 HTTP body 大小。采用可审计的增量 JSON 解析器；若选择第三方库，在 ADR 记录版本、许可证、内存上限和跨 chunk 能力。不能把正则匹配/花括号计数当 JSON parser。
- 任意字节都可成为 chunk 边界，包括 UTF-8、转义、数字、字段名。忽略字段也必须语法正确且跳过深度有上限。
- 深度上限 16；普通 JSON number token 上限 32 bytes；source 字符串超长归 Other；时间字符串上限 64 bytes；不缓存 project/model/session 字符串。
- 使用十进制安全整数解析，不能经过 `double` 再转换后声称保留了超过 `2^53` 的精度。若需兼容整数指数形式，要单独实现和测试，否则明确报 schema 错误。
- 默认单日总流量上限 2 MiB、bucket 数上限 20000；这些是防失控限额，不是服务端数据保证。任一超限报 `BODY_TOO_LARGE` / `TOO_MANY_BUCKETS` 并保留 LKG，不显示被截断的合计。
- 网络任务周期性让出 CPU，响应取消与 watchdog；不要在 LVGL 锁内解析。
- complete JSON 之外的尾部垃圾、重复必需 key、缺少 buckets 或 hasAnyData、错误响应 HTML 均拒绝。
- candidate + tokenizer + HTTP 应用 buffer 的额外 RAM 目标 ≤16 KiB，不包含 TLS/Wi-Fi 栈；在两板测量实际峰值。

### 11.3 受限 buffer 路径：仅用于早期验证

Device Flow 的小响应可用 cJSON。Usage 如果 P0 早期暂用整包 buffer，必须同时设置 `MAX_USAGE_RESPONSE_BYTES=16 KiB` 和 `MAX_JSON_ARENA_BYTES=32 KiB`，含 body、NUL 及 DOM 预算分别计数。无 Content-Length 时也逐块检查；超限主动终止。

不能因选择单日请求就假定 64–96 KiB 的 body 加 cJSON DOM 能塞进 C3。`cJSON` 的 number 依赖浮点，Usage 64 位整数须保留原始数字 token 并安全转换，或只允许安全整数范围且明确拒绝超限。未经这些验证的 cJSON Usage 路径不能作为正式大数据支持方案。

只要真实单日 response 或 heap 门槛无法通过，就必须先完成 streaming 再发布；不以增大 buffer、砍掉 LVGL 或静默截断作为修复。两种解析实现复用相同 fixtures 与输出模型。

### 11.4 C3 内存验收门槛

- LVGL 活跃 + Wi-Fi 已连 + TLS 握手 + parser 工作的全过程，**内部 8-bit 可用 heap 最小值 ≥40 KiB**；记录 largest free block，默认目标 ≥16 KiB。
- 这两个数字是初始门槛；必须记录芯片、固件 SHA、IDF、TLS 栈配置及真实最大响应样本。若不能达到，应优化分配生命周期并复测，不能仅在文档下调数字标记通过。
- 24 小时运行、至少 100 次串行 TLS 请求以及 20 次进入/退出配网后，无持续 heap 下跌、碎片化分配失败或 watchdog。
- AP/Portal 退出后释放 HTTP server、DNS、扫描结果与页面临时数据，再开始 Vibe TLS。遇连接失败要验证释放路径。

## 12. LKG 与业务数据状态

应用生命周期状态与业务数据状态是两条独立轴：例如 `WIFI_CONNECTING + STALE(has_lkg=true)`；不能只用一个 `connected` 布尔值表示全部状态。

| 数据状态 | 条件 | 展示/行为 |
|---|---|---|
| `READY` | 当前窗口有完整可信结果，最新必要同步成功，未超 freshness 门槛 | 显示值、窗口和成功时间 |
| `EMPTY` | 完整 HTTP 200 响应确认当前窗口无 buckets/无用量 | 显示真实 0 与空状态；不得由网络失败推导 |
| `STALE` | 最新请求失败，缓存过期，或当前窗口不完整 | 有 LKG 则保留并标原因/日期；无 LKG 显示 `—` |
| `AUTH_REQUIRED` | 已保存 Key 的 Usage 请求返回 401 | tombstone Key、停止该 Key 的重试；保留 Wi-Fi，提供 Reconnect |
| `LINK_REQUIRED` | 无 Key、主动解绑、code 拒绝/过期、410 且无已保存 Key | 显示开始/重试授权入口 |

`hasAnyData=false` 只有在完整有效 200 响应下才参与 EMPTY 判断。账号有历史但今日空时，即使 `hasAnyData=true`，Today 仍应为已验证的 0；7D 独立根据 7 日日槽决定。必须按第 7 节实测语义实现，不将其当成“今天大于零”。

freshness 默认阈值为 90 分钟。任何新一次失败可立即加 `Sync failed` 标记；超过 90 分钟标 `Data stale`。`READY` 不表示上游采集实时，也不表示 7D 对账永远完整。快照 state 默认描述 Today，7D 页面根据 `seven_day_complete`、日槽成功情况和 reconciliation 年龄派生状态：对账距今超过 36 小时仍未完成时标注过期。

401 时可以在进入授权页前短暂显示“已断开”及旧快照的日期；一旦开始新账号绑定或主动 unlink，旧数据立即隐藏且逻辑失效，不在不同账号之间保留可见历史。权限故障、存储故障和 time invalid 都保留独立 reason，不把它们改写为 0。

## 13. `vibe-usage-ai-passport` 实现

### 13.1 BSP、Flash 与 Recovery

从 `ai-passport` 官方 baseline 衍生，不合并多个 Demo 分支。硬件常量来自 `components/bsp/include/bsp_pins.h` 与 BSP headers；业务代码不再定义 GPIO、I2C 地址或 ADC 按钮阈值。

固定 ESP-IDF **5.5.3**、target `esp32c3`、8 MB Flash、无 PSRAM。以下布局必须保持：

| 分区 | Offset | Size | 合同 |
|---|---|---|---|
| `nvs` | `0x9000` | `0x6000` | 应用和 Wi-Fi 持久化，禁止故障时自动整区擦除 |
| `phy_init` | `0xF000` | `0x1000` | 保留 |
| `factory` | `0x10000` | `0x300000` | 3 MiB app 上限，不是整个 merged 文件的上限 |
| `cardid` | `0x356000` | `0x4000` | 设备 identity，禁止 payload、擦除或覆盖 |
| `recovery` | `0x700000` | `0x100000` | 官方永久 Recovery，禁止替换 |

保留 `bootloader_components/recovery_boot_hook/`，开机 UP 5 秒进入 Recovery。应用不重新实现 BLE installer；禁用应用层 BLE 配网不会禁用独立 Recovery 的安装功能。

小程序安装产物按上游合同为 `build/FoloToy-AI-Passport-full.bin`（从 0x0 起的 merged ESP image）；app-only 文件不能作为小程序产物。社区镜像不得包含设备 cardid 或替换 Recovery。发布前必须用上游检查验证偏移、MD5、保护区和 hook。[Recovery 兼容合同](https://github.com/FoloToy/ai-passport/blob/f913af29a387f8983ef4faa9f1580b14e88b0740/docs/development/engineering/ble-recovery-compatibility.md)

禁止 `erase-flash`。开发时使用经检查的分段烧录参数或小程序安装；merged 文件跨越保护区空洞时不能当作原始整段 Flash dump 直接写入。升级、NVS 迁移和 Factory Reset 都必须证明 identity/Recovery 内容保持不变。

### 13.2 LVGL UI 与按钮

默认使用英文短标签与数字，避免引入大套 CJK 字库；中文 UI 放 P1，用明确 glyph subset 并验证缺字。保留上游单 `240×20` RGB565 DMA buffer（约 9.6 KB）及 24 KB LVGL pool 基线；不添加整屏/双全屏 buffer。[硬件指南](https://github.com/FoloToy/ai-passport/blob/f913af29a387f8983ef4faa9f1580b14e88b0740/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md)

三个主页面：

| 页面 | 内容 |
|---|---|
| Overview | Today 或 7D 大数字、Top 4 sources、占比；底部另一个窗口与最后成功时间 |
| Agents | 当前窗口全部 UI agents，可滚动，含 Other/合并标识 |
| Status | 当前日期/时区、Wi-Fi 状态、同步结果、数据口径 API Total、固件版本、电池可用性 |

```text
VIBE USAGE            14:42
TODAY
12.8M

Codex        6.2M      49%
Claude       3.9M      31%
Grok         1.8M      14%
Other        0.9M       6%

7D 82.2M          Sync 8m
```

示意数字只用于布局，不得进入 release 默认数据。授权/Wi-Fi/错误页是专用状态页。

| 输入 | 行为 |
|---|---|
| UP/DOWN 短按 | Overview/Agents/Status 切页；Agents 内按列表边界切页 |
| OK 短按 | Overview/Agents 切 Today↔7D；Settings 中确认 |
| OK 长按 1.5 秒 | 主页面打开 Settings；子页面返回 |
| Settings → Refresh now | 请求一次 Today，显示同步中并合并重复操作 |
| 开机 OK 2 秒 | 配网快捷入口 |
| 开机 UP 5 秒 | 上游 Recovery，应用不得抢占 |

Settings 固定包含：Refresh now、Reconcile 7D、Wi-Fi、Vibe account、Timezone、Display、About、Reset Vibe settings。解绑与重置各有明确本机确认页，避免误触。

### 13.3 运行模式

**USB/Docked 默认**：15 分钟刷新；无操作 30 秒降背光到初始亮度的 20%；按键先恢复亮度再消费下一次业务输入。页面无连续动画，不用一分钟一次联网来模拟实时。USB 与电池判定只使用 BSP 明确支持的状态；若供电源不能可靠判定，使用 Settings 手工模式，不猜测额外 GPIO。

**Battery 目标**：唤醒立即展示有日期的 LKG，后台更新；90 秒无操作熄屏并停无线电。P0 可先用运行态熄屏完成按钮可恢复闭环。P1 在确认实际唤醒路径后采用 light/deep sleep。

Passport 的三枚功能键是 GPIO0 上的 ADC 电阻梯，上游已验证的是计时器睡眠 Demo，不能据此宣称任意功能键都能从 deep sleep 唤醒。专用电源键与这些功能键不同。必须验证支持的 GPIO 电平、按键和启动模式，再决定最终“按钮唤醒”的具体键；未通过时保留运行态熄屏，不发布不可唤醒的电池模式。

LCD 熄屏期间不为展示不可见数据每 15/30 分钟深睡唤醒；用户唤醒后再同步。缺失电池或 CW2017 读失败时显示未知，不编造电量。不要创建第二条 I2C0 bus 或第二个 ADC1 owner。

## 14. `vibe-usage-zectrix` 实现

### 14.1 BSP 与显示模型

target 为 `esp32s3`；以 ESP-IDF 5.5.3 做统一候选，必须完成 NOTE4 编译、运行与电源验收后再作为发布锁定版本。保留上游 `zectrix_board`、`zectrix_epd` 与需要的 canvas/font 代码，移除 Demo 菜单业务；不引入 LVGL。

上游默认 16 MB Flash、Octal PSRAM 80 MHz，当前 app partition 也为 `0x300000`。记录实际芯片、Flash 和 PSRAM 检测结果；S3 有 PSRAM不代表任意 SPI/DMA buffer 都能直接放进去，遵守所选 IDF/驱动的分配要求。

1bpp framebuffer 为 400×300/8 = **15000 bytes**，MSB first、1 白 0 黑。额外预算包括驱动 shadow、目标帧、dirty rect buffer，不能把 15 KB 误称整个显示系统内存。UI 使用纯黑白、实心条形、稳定布局；不使用 4bpp 灰阶路径。

页面：Overview、Agents、Status/Settings、Wi-Fi Setup、Vibe Link。Overview 显示窗口大数字、最多 4 行 source 条形图、另一个窗口、成功同步的绝对时间和电池。

### 14.2 full / partial refresh 策略

上游 partial 需要此前成功 full 1bpp 建立 shadow；公共 API 没有从持久化旧图恢复 shadow 的入口。[EPD 接口](https://github.com/itopinion/zectrix-note4-epd-demo/blob/ca285c98ed0641f86780edb1f5ec77b0335fe649/components/zectrix_epd/include/zectrix_epd.h)；[驱动实现](https://github.com/itopinion/zectrix-note4-epd-demo/blob/ca285c98ed0641f86780edb1f5ec77b0335fe649/components/zectrix_epd/zectrix_epd.cc)

| 场景 | 动作 |
|---|---|
| 冷启动且画面归属/内容未知 | Full 1bpp |
| 同一驱动实例、shadow 有效、少量区域变化 | Partial 更新变化区域 |
| 目标可见内容无变化 | 不启动 EPD 刷新，保持原图 |
| 每累计 10 次 partial | 下一次有变化时 full，成功后清零；不为计数单独唤醒 |
| 页面/布局/字体变化 | Full |
| 首次显示或更换授权/Wi-Fi QR | Full，确保可扫码 |
| EPD timeout、partial 失败、shadow 不可信 | 标无有效 base；下一次绘制 full |
| deep sleep 重启后，需要画新内容 | Full；不能仅凭旧 NVS bitmap 绕过驱动的 base 检查 |
| deep sleep 重启后，可确认目标仍等于留屏内容 | 可以不刷新；首次真正变化时仍 full |

NOTE4 可以关 EPD rail 后保留物理画面；同一个活进程内 driver shadow 可能仍在，deep sleep 重启则 RAM/driver 实例丢失。**“屏幕还留着旧图”不等于“驱动可安全 partial”。** P0/P1 不修改驱动私有 shadow 以假装已有 base。P2 如需跨 deep sleep partial，必须提供正式 restore API、图像一致性校验及 ghosting 实验。

比较对象为实际可见内容：数值格式化结果、source 排序、窗口/日期、状态、连接提示、电池档位及 QR。正常无新用量时不每 30 分钟改一次屏上 SYNC 时间；旧时间明确表示“本屏数据确认时间”。内部最新成功时间只在 Status 页显示。STALE/AUTH_REQUIRED、日期改变或电量跨档属于有意义变化，应更新。

墨水屏不能显示随时间自动变化的 `8m ago` 或跳秒时钟，使用 `Sync 09-04 14:30`、明确日期。没有持续 UI timer 为“时间过去了”刷屏。

dirty rect 生成须包含旧文字擦除面积，边界裁剪和行 stride 必须正确；驱动内部按 8 像素对齐，不得错把部分矩形 pointer 传成全帧。refresh 成功后才更新 `last_rendered` 和计数；失败时不把候选图标记为已上屏。

### 14.3 RTC、deep sleep、battery latch

已核对的关键连接：PCF8563 I2C 地址 `0x51`，RTC interrupt GPIO5，battery latch GPIO17（高电平保持电池供电）。启动必须在初始化其他外设前拉高 latch。按钮为 OK GPIO0、UP GPIO39、DOWN GPIO18。[NOTE4 硬件表](https://github.com/itopinion/zectrix-note4-epd-demo/blob/ca285c98ed0641f86780edb1f5ec77b0335fe649/docs/HARDWARE.md)

分阶段实现：

1. P0 常开 30 分钟刷新，验证 Wi-Fi、授权、UI、full/partial、LKG。
2. P1a 先采用 ESP32 timer wake，打通完整 sleep/wake 周期。若 battery latch 保持未通过，保持常开，电池功能不得标完成。
3. P1b 再启用 PCF8563 alarm → GPIO5 外部唤醒；需要时扩展 board adapter 的 alarm API。外部 RTC 的存在不证明当前驱动已实现产品调度。
4. P2 才优化跨睡眠 partial、待机功耗与更长续航。

```text
WAKE
 → 尽早保持 battery latch
 → 读取 wake cause、UTC/RTC、配置和 cache
 → 需要时显示 LKG，否则保持留屏
 → STA + 校时（到期时）
 → Today / 受预算限制的 reconciliation
 → 完整校验与持久化
 → 可见内容变化才刷新 EPD
 → 停 HTTP、Wi-Fi；关闭 EPD/audio/NFC/非必要 LED
 → 设置 timer 或 RTC alarm，确认 IRQ 未被旧标志持续拉低
 → 配置经过验证的 latch hold 和 wake pin
 → deep sleep
```

必须分别验证 USB 和电池供电：GPIO17 在 deep sleep 中保持高的实际波形、rail 不掉电、30 分钟后能重启。GPIO hold、RTC IO 设置与解除由 power adapter 管理；不能盲目套用示例 GPIO。RTC alarm 触发后读取/清中断标志，防止立即再次唤醒；RTC oscillator-stop/无效时间必须触发校时。

按钮唤醒不等于运行时三键扫描：ESP32-S3 GPIO39 不是 RTC IO，不能直接照抄为 deep-sleep EXT wake。P1 只启用经过板级验证的 OK/DOWN 等可用路径，记录 UP 在睡眠中不保证有效；应用层不能修改接线。[ESP32-S3 睡眠与唤醒约束](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/sleep_modes.html)

关机与睡眠必须分开：DOWN 长按 3 秒沿用上游“清屏、停外设、释放 latch”的关机语义，不把拉低 latch 当作定时休眠。拉低 latch 后 RTC IRQ 是否能重新上电没有本项目证据，不作此承诺。外接 USB 下 rail 可能继续供电，须测试实际表现。

电量使用 board ADC 的估计值并标为估计；每轮唤醒采样，处理未装电池及充电状态。P1 低电保护阈值必须依据实际电池、测量曲线和 BSP 标定确定并记录，未确定前不设置臆测的截止电压；不得宣称未经测量的续航天数。

### 14.4 按钮交互

| 输入 | 运行态行为 |
|---|---|
| UP/DOWN 短按 | 上/下一页；Agents 内滚动 |
| OK 短按 | Overview/Agents 切 Today↔7D；菜单中确认 |
| OK 长按 1.5 秒 | 主页面进 Settings；菜单内返回/取消 |
| Settings → Refresh now | 手动刷新 Today，合并重复点击 |
| 启动时 OK 2 秒 | 重新配网 |
| DOWN 长按 3 秒 | 清屏并关机，优先级高于翻页 |

UP/DOWN 的短按动作在长按识别时取消，避免关机前先翻页。sleep wake 的第一次按键只负责唤醒，等释放后再接受业务输入，避免按住就连续触发菜单或 Recovery 类启动行为。

## 15. 统一应用状态机

```text
BOOT
 └─ LOAD_CONFIG_AND_CACHE
     ├─ pending reset ───────────────> COMPLETE_RESET → REBOOT
     ├─ storage error ───────────────> STORAGE_ERROR
     ├─ no Wi-Fi / physical setup ───> WIFI_REQUIRED → WIFI_PROVISIONING
     └─ saved Wi-Fi ─────────────────> WIFI_CONNECTING

WIFI_PROVISIONING
 ├─ valid credentials + portal exit ─> WIFI_CONNECTING
 └─ cancel/timeout ──────────────────> WIFI_REQUIRED 或恢复旧 Wi-Fi

WIFI_CONNECTING
 ├─ GOT_IP ─────────────────────────> TIME_SYNC
 └─ budget exhausted ────────────────> BACKOFF / PREPARE_SLEEP

TIME_SYNC
 ├─ time valid + key ────────────────> SYNCING
 ├─ time valid + no key ─────────────> DEVICE_LINK
 └─ time invalid ───────────────────> TIME_REQUIRED / BACKOFF

DEVICE_LINK
 ├─ REQUEST_CODE → SHOW_CODE → POLLING
 ├─ key saved ──────────────────────> SYNCING
 └─ denied/expired/cancelled ────────> LINK_REQUIRED

SYNCING
 ├─ valid 200 ──────────────────────> READY / EMPTY
 ├─ 401 ────────────────────────────> AUTH_REQUIRED
 └─ network/schema/limit error ─────> STALE

READY / EMPTY / STALE
 ├─ schedule/user refresh ──────────> SYNCING
 ├─ settings ───────────────────────> SETTINGS
 └─ NOTE4 power policy ─────────────> PREPARE_SLEEP → SLEEP
```

LKG 在加载后即可呈现，后台状态改变不清空 Dashboard。`WIFI_REQUIRED`、`TIME_REQUIRED`、`STORAGE_ERROR` 为环境/生命周期状态，与第 12 节五种数据状态组合。

状态转换表必须写成纯逻辑 reducer 并有 Host tests：

| 当前事件 | 必须执行的副作用 |
|---|---|
| `USER_RECONFIGURE_WIFI` | 取消当前云任务、保留 auth/cache、进入 Portal |
| `USER_RELINK` | 增 generation、失效旧 auth/cache、联网后申请新 code |
| `HTTP_401` | 持久化 auth tombstone、停止旧 Key 请求、清后台对账任务 |
| `AUTH_SUCCESS` | 先持久化 Key/origin/generation，再清临时 code 并同步 |
| `TIMEZONE_CHANGED` | 停旧请求、改 config revision、重建当前窗口 |
| `HTTP_SUCCESS` | 仅相同 generation/config 的完整候选可提交 |
| `NETWORK_LOST` | 停请求且保持 LKG；根据供电模式退避或休眠 |
| `USER_RESET` | 本机确认后写 reset journal，分阶段完成重置 |
| `POWER_SLEEP` | 等待/取消 worker，确保不在 NVS/EPD 操作中切电 |

Portal 与授权页面不应因后台周期任务抢走界面；手动操作优先，普通自动刷新延期。非幂等的 POST code 请求发生不确定失败时不能高速重复创建；按退避重新申请，并只保留最后一个本地有效会话。

## 16. 仓库结构与依赖管理

### 16.1 `vibe-usage-ai-passport`

```text
vibe-usage-ai-passport/
├── AGENTS.md
├── CMakeLists.txt
├── bootloader_components/recovery_boot_hook/  # upstream 保留
├── components/
│   ├── bsp/                                  # upstream 保留
│   ├── wifi_adapter/
│   │   ├── include/wifi_adapter.h
│   │   ├── wifi_adapter.cc
│   │   └── idf_component.yml
│   └── vibe_usage/
│       ├── CMakeLists.txt
│       ├── include/
│       │   ├── vibe_usage.h
│       │   ├── vibe_auth.h
│       │   ├── vibe_client.h
│       │   ├── vibe_store.h
│       │   └── vibe_ports.h
│       ├── src/
│       │   ├── vibe_auth.c
│       │   ├── vibe_http.c
│       │   ├── vibe_client.c
│       │   ├── vibe_parser.c
│       │   ├── vibe_aggregate.c
│       │   ├── vibe_cache.c
│       │   └── vibe_store.c
│       └── test/
├── main/
│   ├── main.c
│   ├── app_controller.c
│   ├── app_state.c
│   ├── network_worker.c
│   ├── time_adapter.c
│   ├── power_passport.c
│   ├── ui_overview.c
│   ├── ui_agents.c
│   ├── ui_link.c
│   ├── ui_wifi.c
│   ├── ui_settings.c
│   └── qr_render.c
├── tests/
│   ├── fixtures/
│   ├── contract/
│   ├── parser/
│   ├── cache/
│   └── state/
├── tools/                                    # 保留 upstream validate
├── docs/
│   ├── api-contract.md
│   ├── upstream-lock.md
│   ├── core-sync.md
│   ├── adr/
│   └── acceptance/
├── .github/workflows/
├── partitions.csv
├── sdkconfig.defaults
└── dependencies.lock
```

### 16.2 `vibe-usage-zectrix`

```text
vibe-usage-zectrix/
├── AGENTS.md
├── CMakeLists.txt
├── components/
│   ├── zectrix_board/                         # upstream
│   ├── zectrix_epd/                           # upstream
│   ├── wifi_adapter/                          # 相同公共 adapter 契约
│   └── vibe_usage/                            # 相同公共 core
├── main/
│   ├── app_main.cc
│   ├── app_controller.cc
│   ├── app_state.cc
│   ├── network_worker.cc
│   ├── time_adapter.cc
│   ├── epd_canvas.cc
│   ├── epd_refresh_policy.cc
│   ├── screen_overview.cc
│   ├── screen_agents.cc
│   ├── screen_link.cc
│   ├── screen_wifi.cc
│   ├── screen_settings.cc
│   ├── power_manager.cc
│   └── rtc_manager.cc
├── tests/                                    # 共享 fixtures + NOTE4 专项
├── tools/
├── docs/                                     # 与 Passport 相同文档契约
├── .github/workflows/
├── partitions.csv
├── sdkconfig.defaults
└── dependencies.lock
```

普通使用 Component Manager 引入 `78/esp-wifi-connect`，不手改 `managed_components/`。需要随机 SSID、日志或 Portal handler 补丁时，选择固定提交的 fork/受控 vendored override，并记录与上游差异；不在每次构建后临时修改下载缓存。

两仓暂时各保留 `components/vibe_usage` 副本。第一仓为初始实现来源；移植时记录来源 commit、公共 header/fixture/schema 哈希。修复协议/parser/cache 时同时更新两仓并运行相同 Host tests；UI 与电源文件不复制。`docs/core-sync.md` 记录 core revision、已同步修复和兼容版本，防止长期静默分叉。

开发 Agent 进入实际仓库后先读取其 `AGENTS.md` 和任务相关文档，保留用户未提交修改。本文是中文独立交付文件；如果将其纳入保留上游文档规范的 Passport 仓库，再按该仓规则补英文与中文成对版本。

## 17. CI、Release 与升级

### 17.1 每次 PR 的 CI

| Job | 必须检查 |
|---|---|
| source/format | 格式、编译警告、公共 API 与 fixture/schema 版本一致 |
| host-tests | parser、64 位整数、聚合、日滚动、状态 reducer、存储恢复、刷新策略 |
| fixture-contract | 脱敏/合成 fixture 元数据、日期范围及 total 期望值 |
| build | Passport ESP-IDF 5.5.3 + esp32c3；NOTE4 通过验证的 IDF + esp32s3 |
| size/memory | app 实际 partition 边界、静态 RAM、map/size 报告；运行峰值另由真机验证 |
| firmware-layout | Passport 完整保护区/MD5/Recovery hook gate；NOTE4 分区与 flash args 检查 |
| artifact/privacy | 无真实 Key、Wi-Fi 密码、cardid dump、session/project 数据；许可证及来源齐全 |

Passport 保留现有检查入口：

```sh
./tools/validate.sh --static
./tools/validate.sh --firmware
./tools/validate.sh
```

前两项用于分阶段定位，最终运行完整 gate。源自[上游 Agent 验收入口](https://github.com/FoloToy/ai-passport/blob/f913af29a387f8983ef4faa9f1580b14e88b0740/AGENTS.md)。NOTE4 仓新增等价的 `tools/validate.sh` 入口，明确哪些子检查尚未支持。

新建干净构建目录时使用正确 target；不要为了跑 CI 覆盖用户本地 sdkconfig。CI 缓存 key 至少包含 IDF 版本、target、dependencies.lock 和 sdkconfig.defaults 哈希。ESP-IDF image 及 Actions 固定可复现版本/提交。

Host tests 使用 synthetic transport 与 NVS port，不需访问用户账号。真实 Device Flow/网络测试只在明确标记的真机环境执行，CI 日志不注入个人 Key。PR 来自外部贡献者时不给发布凭据。

### 17.2 Release 内容

每个 tag 生成独立设备包，至少包括：

- 固件 app、需要的 bootloader/partition 文件及机器可读的分段 flash 参数。
- Passport 小程序用的 `FoloToy-AI-Passport-full.bin`，经过上游完整 merged 验证；文件名遵循现有合同。
- NOTE4 明确标 `NOTE4 BLACK-WHITE / ESP32-S3 / 400x300`，不得把 NOTE4C 放入兼容列表。
- SHA-256 checksums、固件语义版本、git SHA、IDF 版本、硬件 target、core/schema/metric 版本、依赖锁信息。
- Release notes、安装/恢复说明、升级前后 NVS 兼容性、实测硬件版本和未验证项。
- SBOM 或等价依赖许可证清单；保留复制组件的 LICENSE/THIRD_PARTY_NOTICES。

仅 app 镜像尺寸受其 app partition 上限约束；不要错误地以 merged 文件长度判断 Passport 3 MiB 是否合规。NOTE4 当前沿用 3 MiB factory 分区也须做尺寸 gate，不能因物理 Flash 为 16 MB 就无限扩大 app。

### 17.3 更新策略

P0 不实现自建 OTA。Passport 使用官方 Recovery/小程序与安全的开发分段烧录；NOTE4 使用明确目标与偏移的 USB 流程。两板上游 factory 布局都不能被当成已经提供 A/B OTA。

NVS schema 升级支持“旧版读取 → 校验 → 写新版”；不可兼容的 cache 可以丢弃重建，但 Wi-Fi 和 auth 不应随升级清空。降级遇未知 schema 时停止读取该 blob，显示兼容性错误或仅重建 cache，不能强行解释旧结构。CI 覆盖旧 cache fixtures，真机覆盖一次升级及掉电恢复。

## 18. 安全与隐私边界

1. 固件源码、sdkconfig、Release 二进制、二维码和日志中不得嵌入个人 API Key。运行中 Key 仅在 auth/store/HTTP 范围，UI 不读取。
2. 不记录 Authorization 的任何前缀、尾缀或完整响应；公开 App 里的调试打印方式不能复制到硬件。错误日志只含阶段、HTTP code、错误分类、字节数、耗时和资源计数。
3. 原始 Usage response 不写 Flash、崩溃 dump 或远程 telemetry；跳过的 project/hostname/session 也不得打印。P0 不启用把内存上传到远端的崩溃收集。
4. `vbu_` 的实际权限范围未经服务端明确证明，不能称为 read-only device token。固件代码只调用授权端点及 GET `/api/usage`，并不意味着泄漏后的 Key 也只读。
5. `apiUrl` 和验证 URI 必须限制 HTTPS 与信任 origin，防止服务地址/重定向把 Bearer 发往其他主机。Portal 不提供修改云 API origin 或读出 Key 的接口。
6. 配网窗口开放 AP + HTTP 的残余风险见第 8 节，后续 WPA2 也不等于云端登录；Portal 永远不接收 Vibe 账号密码/API Key。
7. Wi-Fi scan 返回的 SSID 属于不可信字符串，Portal 必须 JSON/HTML 正确转义；表单限制 body 长度、SSID/password byte 长度，避免溢出和页面注入。敏感/变更 handler 仅在物理开启的配网会话可用，不在常规 STA 网络暴露管理服务。
8. P0 不烧写 eFuse、不启用 Secure Boot/Flash Encryption。尤其 Passport 必须先证明 Recovery、小程序、bootloader、NVS encryption 和恢复流程相容；不可为了“安全”执行不可逆配置。
9. 明文 NVS 无法抵抗物理 Flash 读取，逻辑 unlink 不保证历史页安全擦除。说明丢失设备时需在 Vibe 服务撤销授权；若服务不支持逐设备撤销，明确实际能力，不能伪造 revoke 成功。
10. 公开销售/部署到不可信环境前，应推动专用只读硬件 Token；服务端 compact endpoint 也可作为未来优化，但当前不能假定存在 `/api/usage/compact`、`vbr_` Token 或 refresh token。

## 19. 自动化测试与真机验收矩阵

### 19.1 Host/契约测试必须覆盖

| 组别 | 输入 | 必须得到的结果 |
|---|---|---|
| 基本聚合 | 多 source、同一 source 的多 project/model/host | 所有合法 bucket 恰好求和一次 |
| 重复同步 | 同一天先 1000，后 1000 | 最终仍 1000，不翻倍 |
| 服务修订 | 同一天先 1000，后 700/0 | 按完整响应替换为 700/0 |
| 整数 | >2^32、2^53+1、UINT64_MAX 边界 | 精确保留；加法/词法溢出明确失败 |
| 非法字段 | 负数、fraction、null、缺 key、重复 key | 失败且旧日槽不变 |
| JSON 分块 | 每个字节边界、UTF-8/转义跨块 | 与整块输入同结果，无越界 |
| 深度/体积 | 超深 sessions、超大 body、超多 buckets | 有界拒绝，不产出 partial success |
| source 溢出 | >23 source、超长 id | Other 保住总量；明确 collapsed 标志 |
| 时间 | 跨月/年、闰日、午夜、时区切换 | Today/7D 标签和覆盖正确 |
| DST（P1） | 23/25 小时自然日 | 按当地日历划分，不按固定 24h |
| 缺日 | 最近 7 日只拿到 3 日 | incomplete 3/7，不当完整 0/总量 |
| 响应半包 | 最后一个 bucket 中断、chunked 未结束 | candidate 丢弃，LKG 与成功时间不变 |
| 对账失败 | 7 日中 1 日失败 | 成功日可更新，但不推进完成时间；可恢复任务 |
| 异步取消 | unlink/reset/改时区后旧响应到达 | generation 不匹配，丢弃 |
| 存储掉电 | cache A/B、auth tombstone、reset 各阶段中断 | 启动恢复为一致状态，不重新激活旧 Key |
| schema 迁移 | 旧版、未知版、CRC 错误 | 支持的迁移；不支持的安全失效 |
| HTTP/Auth | 200 pending、denied、expired、410、401、403、429 | 状态/节流/凭据处理符合第 6、11、12 节 |
| EPD 策略 | 相同图、布局变化、10 次 partial、重启 | skip/full/partial 选择符合第 14 节 |

### 19.2 真机矩阵

标记：A=Passport，Z=NOTE4，B=两者。每条记录固件 SHA、设备型号/板修订、供电方式、操作、预期、实测、证据文件和 PASS/FAIL/NOT RUN。模拟服务结果与真实 Vibe API 结果必须分栏。

| ID | 设备/阶段 | 场景及通过条件 |
|---|---|---|
| HW01 | B/P0 | 正确识别芯片、屏幕、Flash/PSRAM；NOTE4C 不在支持范围 |
| HW02 | B/P0 | 空 Wi-Fi 首启进入 Portal；iOS/Android 均可完成，自动弹窗失败可手动访问 |
| HW03 | B/P0 | 错密码后能改正；旧网络不丢；SSID 包含中文/引号/32-byte 边界无注入或溢出 |
| HW04 | B/P0 | 2.4 GHz WPA2、隐藏 SSID、同 SSID 多 AP、多已保存网络按预期连接；仅 5 GHz 网络给出明确不支持提示 |
| HW05 | B/P0 | AP 有 IP 但无外网、DNS 失败、SNTP 不通各自显示正确原因；不抹凭据 |
| HW06 | B/P0 | 配网完成和超时退出后 HTTP/DNS/AP 停止；内存恢复，正常 STA 无管理服务暴露 |
| HW07 | B/P0 | 真实 Device Flow：设备/手机代码一致，扫码登录批准，Key 保存，首次真实 GET 200 |
| HW08 | B/P0 | 授权取消、拒绝、过期、重启、410 模拟均能安全重新开始；无高速 poll |
| HW09 | B/P0 | 服务端撤销测试 Key 后真实或受控模拟 401：停止旧 Key 重试，保 Wi-Fi，重新绑定成功 |
| HW10 | B/P0 | 换账号后旧数据隐藏且缓存 generation 隔离，不能看到混合总量 |
| HW11 | B/P0 | 空账号/历史有数据但今日空/多 Agent 三场景正确区分，0 只来自成功响应 |
| HW12 | B/P0 | 真实 Today/7D 与同范围原始 total 求和一致；App 口径差异另记 |
| HW13 | B/P0 | 模拟延迟上传、下降修订、跨午夜、离线 2 日后恢复：reconciliation 正确且不重复累加 |
| HW14 | B/P0 | 429/Retry-After、500、TLS 错误、半包、超限保 LKG，错误不刷新成功时间 |
| HW15 | B/P0 | 断电重启能恢复最近持久化快照；中途掉电不丢旧有效缓存，不自动全擦 |
| HW16 | B/P0 | 最长凭据 + 满 source/8 日日缓存 + 1000 次 NVS 逻辑更新，24 KiB 分区不因 GC 卡死 |
| HW17 | B/P0 | 20 次进入/退出配网和设置，无孤儿 task、HTTP socket、事件回调或 heap 持续下降 |
| HW18 | A/P0 | 真实 TLS+LVGL+大响应，内部 heap 与 largest block 达第 11.4 节门槛 |
| HW19 | A/P0 | app≤0x300000，merged gate PASS；小程序真实安装成功，UP 5 秒可进 Recovery |
| HW20 | A/P0 | 安装、升级、应用重置前后 cardid/Recovery 校验一致；记录脱敏 hash，不保存受保护区 dump |
| HW21 | A/P0 | 无电池/电池读失败不崩溃；背光 30 秒降低，恢复后按钮不误操作 |
| HW22 | A/P1 | 电池熄屏与实际支持的按键唤醒经过验证，90 秒策略正确，无不可唤醒状态 |
| HW23 | Z/P0 | 冷启 full、同实例 partial、页面切换 full；100 次变化/无变化序列无持续残影失控 |
| HW24 | Z/P0 | 目标画面相同不刷；错误/日期/授权变化需要刷；QR 三次手机扫码均可成功 |
| HW25 | Z/P0 | BUSY 超时/partial 失败后不假记渲染成功，下一次 full 可恢复 |
| HW26 | Z/P0 | DOWN 3 秒关机不被短按抢占，清屏与外设停机符合上游；USB/电池分别记录 |
| HW27 | Z/P1a | ESP timer 连续 48 次真实 30 分钟电池唤醒，统计成功率及调度偏差，不以加速循环替代 |
| HW28 | Z/P1a | deep sleep 前后实测 GPIO17/latch rail 保持；记录睡眠电流、刷新峰值和每周期能耗 |
| HW29 | Z/P1a | 离线唤醒在 60 秒预算内返回睡眠；对账在 120 秒预算内保存进度；不长期起 AP |
| HW30 | Z/P1b | PCF8563 alarm + GPIO5 连续定时唤醒，清 IRQ 后不立即循环；无效 RTC 可联网校时 |
| HW31 | Z/P1 | deep sleep 后首次变化 full；有效留屏且无变化可以不刷，状态没有越过 shadow 合同 |
| HW32 | B/P0-P1 | 升级/缓存迁移/中断重置不误清 Wi-Fi 或恢复失效 Key；串口和产物无真实秘密 |

24 小时稳定性要求中可复用 HW17/HW18/HW27 的同一次运行证据，不为数量重复做无意义测试。屏幕证据用照片/录像，电源证据用实测波形/电流，API 正确性用脱敏原始响应及整数对账；三者不能互相替代。

## 20. P0 / P1 / P2 实施顺序

### P0：可安装、可绑定、数据正确的双固件

严格按以下依赖顺序实施；每步可形成一个可审查提交，不把硬件专项提前塞入公共 core。

| 步骤 | 任务 | 完成条件 |
|---|---|---|
| P0-1 | 建立两仓基线，读 AGENTS，锁 IDF/依赖/分区和源 SHA | 上游 build/static gate 通过；硬件与安装边界有记录 |
| P0-2 | 获取/制作 API 契约 fixtures，确认日期、空状态和 total 口径 | 第 7.1 节事实明确；纯逻辑测试可运行 |
| P0-3 | 在 Passport 接入 wifi adapter 和现成 Portal | 首启、保存、重连、退出、重配跑通；无需重写配网栈 |
| P0-4 | 实现时间服务与 Device Flow | 真实授权成功；错误/过期/410 有受控测试；NVS auth 原子写入 |
| P0-5 | 实现 GET 单日、streaming/受限解析、64 位聚合 | 真实单日对账通过；超限/半包保 LKG；C3 heap 合格 |
| P0-6 | 加 8 日 cache、Today/7D、daily reconciliation | 缺日/修订/午夜/重启/时区变化测试通过 |
| P0-7 | 完成 Passport Overview/Agents/Status/Settings 与 USB 模式 | 按钮无阻塞；3 MiB/Recovery/小程序安装 gate 通过 |
| P0-8 | 同版本 core 移植 NOTE4，优先同一 Wi-Fi 组件 | auth/parser/store fixtures 完全一致；显示替换成功 |
| P0-9 | NOTE4 常开 30 分钟刷新 + full/partial + 无变化不刷 | EPD 基线及 LKG 验收通过；关机保留上游语义 |
| P0-10 | 完成 CI/Release、用户说明及 P0 真机记录 | 明确通过项和未测项；正式包仅声明已通过能力 |

P0 包含首次授权二维码和 Wi-Fi 入口，不能把“可用二维码”全部延后为 P2。P0 NOTE4 常开是明确的阶段结果，不宣称电池低功耗目标已经完成。若当前单日数据超过受限 buffer，P0-5 必须完成 streaming 才可关闭。

### P1：产品运行模式与电源闭环

1. NOTE4 timer wake + 电池 latch hold + 预算控制 + 外设停机，完成 24 小时/48 周期电池实测。
2. NOTE4 PCF8563 alarm + GPIO5，完成时间有效性与唤醒循环测试；保留 timer 模式作为已验证回退配置。
3. Passport 电池运行模式，先验证准确的唤醒按钮与供电判定，再启用 sleep。
4. Wi-Fi Portal 的随机 WPA2 密码、SSID/password 边界及手机兼容增强；保持小体积页面。
5. 时区列表和 DST、中文 glyph subset、可选 estimatedCost 显示。费用增加独立 metric/currency/精度契约，不从 tokens 猜价格。
6. 继续 7 天常规使用观察，覆盖每日对账、离线恢复、NVS 写频、内存和电池。

P1 的电源能力未过 HW27–HW31 时，Release 继续保持常开配置且明确限制；不要仅因代码调用了 `esp_deep_sleep_start` 就关闭任务。

### P2：稳定后的扩展

- 满足下一节条件后抽取 `vibe-usage-esp-idf`，两固件改用固定版本公共组件。
- 推动 Vibe 服务提供只读 hardware token、逐设备撤销和有版本 compact endpoint；以新 API 契约实现，不假造现有路径。
- NOTE4 跨休眠 partial 支持、更严格 ghosting/能耗优化、可选 NFC 辅助链接。
- 与厂商安装链一起验证 Secure Boot、Flash/NVS Encryption 及恢复策略；未经授权和设计验证不写 eFuse。
- 独立 OTA/回滚方案需重新设计分区及安装兼容，并保留可恢复路径。

## 21. 抽取 `vibe-usage-esp-idf` 的条件与步骤

以下条件全部满足才抽取，避免在硬件约束和协议尚未稳定时做公共框架：

1. 两块真机都完成 P0，且各自目标运行模式已有明确测试结果。
2. Device Flow、单日请求、日期边界、Token metric、错误映射至少经过一次双端发布验证。
3. parser 对大响应和 64 位数字的测试相同，C3 资源门槛通过；不得以 S3 PSRAM掩盖公共核心的无界内存。
4. NVS schema 已做一次迁移/损坏/掉电恢复测试，账号/时区隔离稳定。
5. 两仓 core diff 不含 UI/GPIO/LVGL/EPD/休眠实现；所有平台差异都通过明确 ports。
6. 连续 7 天真实使用没有未关闭的数据正确性、授权、重启恢复阻塞问题。
7. 重复维护已经产生实际同步成本，公共组件版本和发布责任明确。

抽取顺序：先建立 `vibe-usage-esp-idf` 的纯逻辑测试和 ESP-IDF component 描述；迁入已验证 core 与 fixtures；固定发布版本；逐仓替换引用并复跑 CI/设备冒烟；最后删除重复副本。公共包包括 auth/client/parser/aggregate/cache/store ports，Wi-Fi adapter 可作为可选伴随组件；UI、BSP、RTC 调度和电源始终留在固件仓。

使用 SemVer；公开 header、缓存 schema 与 metric 分别版本化。公共组件升级 PR 同时附两板 build/fixture 结果及受影响的真机门槛，不能只证明组件独立编译成功。

## 22. 当前待验证事项与 Agent 开工清单

### 22.1 已核对与尚未证明的边界

| 事项 | 当前结论 | 关闭方式 |
|---|---|---|
| 上游硬件、组件结构、Passport 分区 | 本文已核对公开源码 | 实际 checkout 与硬件检测再次匹配 |
| 小智同板配网 | 上游 README 记录基础启动/配网验证 | 本固件 HW02–HW07、HW17/HW18 |
| NOTE4 + 首选 Wi-Fi 组件 | 合理的移植选择，尚无本项目实测 | 优先集成测试，失败按第 8.4 节 fallback |
| Device Flow 字段与 HTTP 410 | 公开 App 已实现这些契约 | 真实授权及异常 fixtures |
| from/to、days、hasAnyData、分页 | 客户端模型不足以证明服务端全部语义 | P0-2 的真实只读契约核对；未确认不宣称 7D 正确 |
| API Total 与 App Total | 当前源码确有不同聚合公式 | 固定 API_TOTAL_V1，分别对账，不静默混用 |
| Key 是否只读、是否可逐设备撤销 | 未有足够服务端证据 | 服务端权限文档/授权测试，P0 不作只读承诺 |
| C3 TLS 峰值 heap | 本文只有门槛，没有测量值 | HW18 实测 |
| Passport 电池功能键唤醒 | 上游计时器 Demo 不能证明 | HW22 按具体板级路径验证 |
| NOTE4 30 分钟 deep sleep/latch/RTC | board 存在接口不代表周期产品模式完成 | HW27–HW31，分别测 USB/电池 |
| 24 KiB NVS 的容量与 GC | 已给固定模型及预算，尚未在设备测过 | 最长凭据/缓存/掉电/1000 次更新验证 |
| 低电阈值及续航 | 没有可用的本项目实测数据 | 实际电池标定、电流/能耗记录后再确定 |

这些待验证项不妨碍进行目录、公共 core、mock、UI 和 CI 的开发；它们阻止的是相应功能被错误标记为“已验证可发布”。

### 22.2 Coding Agent 每次开始工作

1. 阅读当前仓库 AGENTS、本文相关章节、`docs/upstream-lock.md` 和最近验收记录。
2. 检查 branch/worktree/status，保留用户改动；确认本次任务位于 P0/P1/P2 的哪一步。
3. 确认 MCU/IDF/依赖及分区。任何烧录前重新发现设备和端口，不能使用历史串口名。
4. 先实现能证明行为的最小闭环，复用现有配网/BSP；对纯协议和状态逻辑增加有意义测试。
5. 运行当前变更所需门槛，再在交付时运行仓库完整 gate。
6. 保存脱敏证据到 `docs/acceptance/`；不将账号 Key、Wi-Fi 密码、原始 personal response 或 cardid dump 保存为测试材料。
7. 交付说明列出完成步骤、具体行为、测试结果、未测项目和下一个有依赖关系的步骤。仅在具备真实证据时标记 PASS。

### 22.3 交付状态模板

```text
Firmware / commit:
Hardware / board revision:
ESP-IDF / dependency lock:
Completed milestone:
Build: PASS | FAIL | NOT RUN
Host tests: PASS | FAIL | NOT RUN
CI: PASS | FAIL | NOT RUN
Real Vibe API: PASS | FAIL | NOT RUN
Device tests: PASS | FAIL | NOT RUN
Installer / Recovery: PASS | FAIL | NOT RUN | N/A
Power / wake: PASS | FAIL | NOT RUN
Peak internal heap / largest block:
App size / partition limit:
Known limitations and evidence paths:
Next implementation step:
```

本方案的最终完成标准是两个独立 ESP-IDF 固件都能从首次配网、手机授权、直接取数运行到正确的 Today/7D 展示及故障恢复；Passport 保持可恢复安装链，NOTE4 低功耗能力由真实电池周期测试证明。
