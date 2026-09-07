# 智能 TODO 固件开发与续做说明

本次在 Vibe Passport / Vibe Note 现有固件上加入语音待办、LLM 局域网配置、声音提醒和 NFC 配网。使用步骤见[用户指南](smart-todo-user-guide.zh_CN.md)，配置接口见[LLM 配置](llm-configuration.md)，硬件与 NFC 写卡见[硬件说明](smart-todo-hardware.md)。设计决定见 [ADR 0002](adr/0002-voice-todo-and-configured-llm.md)。

## 组件与调用链

```text
OK long / release ─→ app_controller ─→ board_audio (ES8311, 8k mono)
                            │                   │
                            │     multipart or Base64 WAV / ASR endpoint
                            │                   │
                            ├─→ Chat Completions ← transcript + current items
                            │          │
                            │     validated action
                            ├─→ smart_todo → NVS → copied UI view
                            │
                            ├─→ deadline scheduler → local speaker chime
                            └─→ Wi-Fi adapter → NDEF / setup QR → LAN LLM portal
```

| 目录 | 责任 |
| --- | --- |
| `components/smart_todo` | 纯待办模型、JSON 操作校验、版本化存储、ASR/Chat 流式客户端 |
| `components/llm_portal` | 独立 LLM 配置存储、Endpoint 校验、带会话保护的本地网页 |
| `components/board_services` | 两板 ES8311 音频、NOTE4 I2C NFC、纯 NDEF 编码 |
| `components/app_controller` | 意图、串行网络任务、取消、提醒调度、页面状态 |
| `firmware/*/main` | TODO 分页、长按/松开、LLM 二维码、提醒可视反馈 |
| `tools/nfc-setup.py` | 为 Passport 被动标签生成一次性 WSC+URI NDEF 和手机写入字段 |
| `tools/generate_todo_fonts.py` | 独立动态标题字库；不改变 UI 字体基线 |

控制器公开 `APP_INTENT_TODO_VOICE_START`、`APP_INTENT_TODO_ACK`、
`APP_INTENT_LLM_CONFIG`、`APP_INTENT_LLM_CONFIG_CLOSE`，松开使用
`app_controller_voice_stop()`，不排队等待网络完成。`app_controller_view_t`
只包含待办和状态，不包含 Key 或录音。大型 view 使用单个串行 scratch，
避免把新数组压入每个网络函数的栈。HTTP 回调可能服务到期提醒；返回后
须重新读取 view，不能覆盖提醒状态。

## 语音事务

1. 长按事件入队时原子占用一次语音请求；同一请求期间拒绝重复启动。
2. 检查本地 TODO 存储可用、LLM 已启用，并打开麦克风；此时不发网络请求。
3. 播放短音并发布 Listening。按住期间以 8 kHz、单声道、16 位 PCM 录音，
   最长 8 秒，写入专用 `voicebuf` 数据分区；断网不打断本地录音。
4. 松开后关闭麦克风，加载配置快照并检查 Wi-Fi。根据 `asr_upload` 选择
   multipart WAV 或 Base64 WAV JSON；Auto 对 OpenRouter 选择 Base64，其他
   Provider 选择 multipart。两种方式都只上传实际录音长度。
5. ASR 无有效 HTTP 状态时可用同一份本地录音安全重试一次，不重新录音。
6. 完整 HTTP 200、完整且有界 JSON 的 `text` 才进入 Chat。过短或静音
   不发出待办操作；识别服务仍可能出错，不能用静音阈值代替真机语音验收。
7. Chat 使用 `model`、`messages`、`stream:false`、`max_tokens:400`。
   读取 `choices[0].message.content`，仅接受正常 stop 结束的单个结果。
8. 本地解析、生成副本、应用并保存；保存成功后才替换 RAM 与刷新屏幕。
   断网、重配、关闭配置、关机等取消路径在保存前再次检查取消状态。

Chat 请求不强制供应商专有工具调用或 `response_format` 扩展，系统提示要求
只输出 JSON，固件仍独立验证输出。端点必须支持标准 Chat Completions
消息结构。它不是旧式 `/v1/completions` prompt 接口或 Responses 接口。
默认模型只是可编辑预填值；服务账号权限、模型存在性由真实接口验收。

### 操作 JSON

```json
{"action":"add","title":"散步","delay_seconds":7200,"repeat_seconds":0}
```

```json
{"action":"add","title":"喝水","delay_seconds":0,"repeat_seconds":3600}
```

```json
{"action":"complete","ids":[1,2,3]}
```

```json
{"action":"delete","ids":[4,5,6]}
```

```json
{"action":"noop"}
```

- 同一次完成或删除可以处理多条，旧版单个 `id` 格式仍兼容。`ids` 最多 12 个；
  重复或不存在的 ID 会被忽略，其余有效 ID 继续执行。整批没有有效变化时不修改列表。
  未知字段、重复字段、未知动作、非法 UTF-8、嵌入 NUL、小数/负数 ID、
  非数组 `ids` 及嵌套冒充对象均拒绝。
- 标题至多 96 字节；列表最多 12 条，完成项也计入容量。删除后按当前顺序重排为 1 到 N。
- 相对时间单位是秒；上限 366 天；重复间隔最短 60 秒。`delay_seconds=0`
  且存在重复间隔时，第一次在一个间隔之后提醒。
- 固定周期覆盖每小时/每天/每周；“每周一早晨”一类日历规则及指定钟点未实现，
  应返回 noop，不猜测日期。模型未正确遵循时仍受结构/范围约束。
- 完成会清除对应项提醒；删除先按操作前的序号统一匹配，再一次性移除并重排，
  不受中途序号移动影响。语音中的对象不明确时返回 noop，不在固件侧近似匹配标题。

## 持久化与时钟

`vibe_todo/list_v1` 是固定 1468 字节的显式小端编码，含 `VTOD`、版本、
revision、next_id、12 个槽和 CRC32。结构体 padding 不进入存储格式。
读取损坏、NVS 空间不足或保存失败均可见；失败不会被包装成空列表成功。
`vibe_llm/settings_v1` 单独保存配置及 CRC；`asr_lang_v1` 保存可选的 ASR
语言代码，空字符串表示请求中省略 `language`。原 `vibe_cfg/settings_v1` 和
`language_v1` 二进制格式保持不变。出厂重置通过既有 journal 清理新增
命名空间，中断重启继续清理；不整片擦 NVS。

提醒使用 UTC epoch，只有时钟有效才触发。每 250ms 控制器循环检查；
已有 Usage HTTP 请求有总时限，临近提醒前暂停新的 Usage 工作。LLM
网络块之间也检查提醒，正在录音时延后到麦克风关闭，避免提示音混入识别。
这些是源码调度策略，不是实测时延承诺。

提醒先成功播放、再持久化下一次截止时间。单次提醒清空 due，重复提醒
跳到原计划中下一个未来时刻，不因断电积压连续响几十次。扬声器失败或
落盘失败保留截止时间，60 秒后重试；声音之后突然掉电可能重响，采用
至少一次提示语义。完成状态不因提醒变成完成。

设备有电且程序运行时可提醒。Passport 仅暗屏，不会为了省电自动停掉
本调度器。NOTE4 用户主动关机/断电期间不会播放声音；尚未引入 RTC
唤醒、电源锁存自启动或深睡闹钟验收。重启后校准时间再补一次逾期提醒。

## 验证命令

```bash
./tools/test-smart-todo.sh
./tools/test-smart-llm.sh
./tools/test-app-smart.sh
./components/llm_portal/tests/run.sh
python3 components/board_services/tests/test_ndef.py
./tools/validate.sh --static
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh --firmware
./tools/validate.sh
```

全量 gate 生成 `build/verified/` 下两个 merged image，执行布局、3 MiB
大小、Recovery hook、关键栈帧和实际 LVGL/canvas 渲染检查。它不会自动
烧机。物理验收必须分别记录固件 hash、芯片/内存、供电、输入动作、
观察结果；参见[本次验收记录](acceptance/2026-09-06-smart-todo.md)。

需要重点复测：C3 TLS+音频峰值堆和最长连续块；麦克风实际声道/音量；
短按与长按边界；EPD 慢刷新期间松开；模型返回错误与弱网；NVS 接近满；
Android/iOS NFC；重复提醒、断网和真实断电恢复。

## 参考资料

- [OpenAI Chat Completions API](https://developers.openai.com/api/reference/resources/chat)
- [OpenAI Audio transcriptions API](https://developers.openai.com/api/reference/resources/audio/subresources/transcriptions/methods/create)
- [ESP-IDF 5.5.3 HTTP Stream API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/protocols/esp_http_client.html)
- [ESP-IDF NVS](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/nvs_flash.html)
- [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 与本地锁定 BSP、ES8311、被动 NTAG213 硬件说明。
- [NOTE4 官方演示](https://github.com/itopinion/zectrix-note4-epd-demo) 与本地锁定 I2C、电源、NFC block 写入代码。
- [78/esp-wifi-connect](https://github.com/78/esp-wifi-connect)：沿用现有 SoftAP 和配网服务器，不另建 Wi-Fi 账户存储。

所有网络引用于 2026-09-06 查阅；本地来源锁定信息见[上游记录](upstream-lock.md)。

## 串口诊断与提交结果（续检补充）

每次语音请求结束输出一条固定数值诊断，不记录 endpoint、Key、录音、
识别文字、标题、待办 ID 或服务原始错误文本：

```text
voice phase=4 committed=1 http=200 system=0x0 cancelled=0 heap_internal=... heap_min_internal=... largest_internal=... stack_free=...
```

`phase` 的 0/1/2/3/4 分别对应前置检查、ASR、Chat、动作校验与应用、
持久化。`committed=1` 只在保存成功并替换 RAM 后设置；`http` 是当前
阶段的 HTTP 状态，0 表示尚未得到状态码。`system` 是本地 ESP 错误码。
因此 ASR 鉴权失败可见 `phase=1 http=401 committed=0`，而 NVS 失败
可见 `phase=4 committed=0`。这些字段不能替代真实模型输出正确性的验收。

`heap_min_internal` 来自 ESP-IDF allocator，自本次启动以来的内部堆低水位，
不是单次语音的精确峰值；`heap_internal` 和 `largest_internal` 是结束时的
当前数值，`stack_free` 是控制器任务的栈低水位。比较多个指令时保留启动
边界，并记录是否已有 Usage TLS/显示等工作，不把所有差值归于录音。

取消在保存前检查。已经成功持久化后才到达的取消信号不撤销该条待办；
此时屏幕仍显示操作成功，日志允许同时为 `committed=1 cancelled=1`，
避免把成功误报成取消而导致重复录入。保存已经开始后的电源/存储语义
仍由 NVS 保证，不能通过一个原子取消标志承诺回滚已提交的数据。

## 订阅主机中继

`services/codex_gateway/` 将现有设备 HTTP 协议转换为官方 Codex App Server
订阅请求。`codex_client.py` 管理受限的 ephemeral 会话与严格 TODO 输出；
`voice.py` 通过 WebRTC v3 播放完整 WAV，只返回最终用户文本；`gateway.py`
提供两条兼容路由、专用 Token、限长输入、单请求并发和取消清理。

配置页的订阅预设只生成现有 URL/model/key 字段，不改变 `settings_v1`。
从保存的两个路由及别名可推断预设。切换预设不会保留未保存 Key；修改
手动 Endpoint 且未输入新 Key 时，页面发送对应 clear 字段。原始设置 API
的空 Key 保留语义不变，API 调用者需自行明确清除。

Voice 没有可用的音频 commit RPC，完整播放后的四秒尾部静音是经验窗口。
只见 partial、观察到的后续片段未完成、转写前后不一致、委派或超时均拒绝。
关闭会话期间再检查迟到事件；断连和 shutdown 重叠取消也必须等待资源清理。
Codex 任务不应拥有 shell、MCP、插件、环境或工作区能力。

中继每秒发送合法 JSON 空白心跳，最后发送单个 JSON 对象；它不是 SSE。
headers 发出后才发生的错误会保留 HTTP 200，但 body 仅含 `error`，没有
`text` 或 `choices`，固件必须按 schema 失败，不能仅据 200 判成功。

宿主测试单独运行，不要求登录、设备或真实模型调用：

```bash
VIBE_GATEWAY_PYTHON=/path/to/gateway/venv/bin/python ./tools/test-codex-gateway.sh
```

CI 新增独立 gateway Host job，固件全量 gate 仍负责 C/C++ 与双板构建。
真实订阅验收见[记录](acceptance/2026-09-06-codex-subscription.md)，启动及
依赖版本见[中继说明](../services/codex_gateway/README.zh_CN.md)。
