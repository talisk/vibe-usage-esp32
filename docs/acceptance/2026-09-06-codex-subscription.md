# Codex subscription 接入验收 — 2026-09-06

本记录是订阅接入的当前结论，替代此前可行性文档中“尚未证明 ASR”的判断。
工作树基于 `b8109a0706e56e036064f05a61ad5e5895bee6cd`，改动未提交、未发布。

## 实现范围

- 双板共用的 LLM 网页增加 Codex subscription 实验性预设，填写主机地址和
  专用 Token 后生成 Chat/ASR 路由及别名；无需改动 NVS ABI。
- 新增 `services/codex_gateway/`：官方客户端订阅 LLM、Voice WebRTC v3 ASR、
  严格 TODO schema、私有 Token、单请求并发、JSON 空白心跳及取消清理。
- 原手动 API 配置仍可用。网页在地址变化且新 Key 为空时显式清除旧 Key；
  原始 API 的空 Key 保留语义不变。切换预设要求替换/明确清除凭据。

## 验证状态

| 层级 | 结果 | 证据及范围 |
| --- | --- | --- |
| Build | PASS | ESP-IDF 5.5.3 双板 clean build、分区与 merged 边界、Recovery hook、栈检查 |
| Host tests | PASS | 完整 `./tools/validate.sh`；gateway 独立 44 项测试；真实 LVGL/Note canvas |
| CI | NOT RUN | 新增独立 gateway job；未推送、未获得远端 CI 结果 |
| Real API | PASS（主机合成输入） | 现有 ChatGPT 登录、无 API Key，ASR → LLM 新增及完成/删除 |
| Device tests | PASS（启动冒烟） | 两板启动标记可见，20 秒内无 panic 或初始化致命错误；交互功能未验收 |
| Installer tests | PASS | 两板只读识别、三段写入及逐段 hash 校验成功；NVS 与保护区未写入 |
| Power tests | NOT RUN | 未做电池、休眠、掉电及长时提醒测量 |

## 真实订阅请求

使用官方 `codex-cli 0.153.3`，`codex login status` 为 ChatGPT 登录。中继
子进程移除 API Key/显式账户 Token 环境变量，不读取或复制账户凭据。
使用现有官方客户端认证存储；自定义 provider/origin 配置会拒绝启动请求。
主机 Python 3.12.14、aiohttp 3.13.3、aiortc 1.15.0、PyAV 17.1.0。

先前低层探测：v1 在服务端协议握手失败，v3 成功。要求 Voice 完全静默的
提示只能得到用户 partial，当前中继会失败。调整为允许简短确认后，英文
合成音频完整转写为 `The blue lamp is next to a green book.`，耗时 16.70 秒，
无意外任务。助手文本与音频均未返回设备，也未在主机播放。

最终另行执行真实 CLI 启动冒烟：版本检查通过；自动创建 0600 专用 Token；
未认证请求返回 401；SIGINT 停止后进程退出。该检查不含模型调用。

主流程在临时 loopback HTTP server 上执行真实路由，客户端设置单次读取
超时五秒、请求总时限 45 秒。请求体匹配固件的 WAV multipart 与 Chat
Completions 结构。认证 Token 仅在内存中生成，关闭测试后监听已停止。

中文音源由 macOS Tingting 合成“**两小时后提醒我喝水。**”，原发音 2.326 秒，
转成 8 kHz 单声道 PCM16 并补静音至八秒，WAV SHA-256：
`f3f3ae2f968d88b321518bbf1802178afa44fde505081846e1a854e1436fbfbc`。
未使用麦克风、个人录音或真实待办。

| 请求 | 最终结果 | 耗时 | 最大读取间隔 |
| --- | --- | --- | --- |
| ASR，中文合成 WAV | `两小时后提醒我喝 水。` | 19.69 s | 1.00 s |
| LLM，直接使用上述转写 | add，title=喝水，delay_seconds=7200，repeat_seconds=0 | 10.99 s | 1.00 s |
| LLM，完成虚构 ID 7 | complete，id=7 | 9.56 s | 1.00 s |
| LLM，删除虚构 ID 7 | delete，id=7 | 8.62 s | 1.00 s |

四次 HTTP 均 200，分别收到合法最终 JSON，响应体含心跳也均小于 4096 字节。
中文 ASR 有词间空格，意图仍被正确解析；这不是识别准确率统计，也不是
真实设备已提交 NVS 的证据。ASR/Chat 各自耗时满足当前固件单阶段期限；
弱网、不同模型及真实录音上传时间仍需实测。

## 故障与清理测试

44 项 gateway Host 测试覆盖：请求鉴权先于启动 Codex；multipart/WAV/JSON
限长与类型；认证前禁用压缩展开；单并发 429；预检认证失败；开始心跳后
的错误 body 无 text/choices；断连取消与 shutdown；provider/环境限制；
严格模型输出与完成状态；意外任务及客户端 RPC 拒绝；进程组后代回收。

语音测试覆盖完整录音播放、最终用户文本、忽略助手、迟到未完成片段、
前后不一致、UTF-8 长度限制，以及 stop/close 期间的新错误/委派/转写。
重复取消仍等待有期限的清理；stop/close 失败不能返回成功。最终清理改动
由生命周期 mocks 验证，不把 mocks 当作真实服务失败注入。

## 当前构建产物

最新文件位于 `build/verified/`，当前清单为
`codex-subscription-candidate.json`。旧 smart-todo 清单及原验收日志保留为
历史记录；其同名 merged 文件已被本次构建替换，不能继续使用旧 checksum。
当前 hash 与源码文件清单见该 JSON。

| 板型 | App 字节 | Merged SHA-256 |
| --- | --- | --- |
| Vibe Passport | 2842976 | `ec2e99e77cba47263c92590f50f73e35ee4c7e245fd0f1db790ca380fb68db8c` |
| Vibe Note | 2909984 | `114ef8f0cbb4b09ba27df43be233c653be9cf992443845736504d23a210a3c73` |

固件源码集合 SHA-256：`f583ed369a3bf07dbd73394884e0751bf64000a83e54920cc673d36325032916`。
中继源码集合 SHA-256：`d0ee249156c17d98d98912b56ab65edc0525860fadd54a122b7f74f35dcade77`。
本地完整构建日志、44 项测试日志及脱敏合成 HTTP 结果保存在
`build/verification-logs/codex-subscription-*`，对应 checksum 记录在清单。

## 两板安装与启动冒烟

用户于 2026-09-06 明确要求烧录两台设备。写入前重新枚举串口并逐台只读
识别，`/dev/cu.usbmodem101` 确认为 ESP32-C3、8 MB Flash 的 Vibe Passport；
`/dev/cu.usbmodem1101` 确认为 ESP32-S3、16 MB Flash、8 MB PSRAM 的 Vibe Note。
USB 标识和芯片地址未保存在验收产物中。

使用 ESP-IDF 5.5.3 和仓库 `tools/flash.sh`。每台设备先通过 layout dry-run，
随即重新枚举端口；安装器再次只读核验芯片和 Flash 后，仅写入 `0x0`
bootloader、`0x8000` 分区表和 `0x10000` factory app。未执行整片擦除，
未写 NVS；Passport 的 `0x356000` 身份区和 `0x700000` Recovery 区未触及。

| 板型 | App SHA-256 | 安装结果 | 启动观察 |
| --- | --- | --- | --- |
| Vibe Passport | `c02bef4ee4f61b0500b012f938c5cf0517603b7f0d821dad777fafb183a6070d` | 三段写入与 hash 校验 PASS | 20 秒内 boot 可见，无 panic/fatal |
| Vibe Note | `8b4ff66727d8c96ddde10213c4068798afad0b1e315c6f198e8b1c45bdedf3b2` | 三段写入与 hash 校验 PASS | 20 秒内 boot 可见，无 panic/fatal |

两台设备通过 USB 供电完成安装和短时启动观察。串口观察只统计产品 boot、
panic 和初始化致命错误，不留存网络、地址、凭据、录音或个人数据。这项证据
证明安装传输和基本启动，不证明屏幕、按钮、麦克风、扬声器、Wi-Fi、NFC、
订阅中继可达性或 NVS 旧设置内容正确。

## 未验证与使用限制

- Voice 和 Codex 任务使用各自额度；本记录不证明所有订阅计划都有权限。
- v3 不是 transcription-only API，也没有音频 commit RPC。发送完整录音后
  四秒静音是经验等待，不能证明任何网络情况下服务端都已处理全部音频。
  当前实现拒绝已观察到的未完成/不一致文本，但不能排除 ASR 本身听错。
- 两板已烧录并通过启动冒烟；仍需分别测试配置页、LAN 可达性、8 秒语音、
  松键、音频质量、堆峰值、失败保留待办及重启持久化。
- 中继必须在线；本轮只启动过临时 loopback 验证服务，没有安装常驻服务，
  也没有将电脑的局域网端口长期暴露。
- 电脑/ESP32 均不应存储订阅令牌副本；普通 NVS 中仅保存中继 Key。中继与
  配置页使用局域网 HTTP，按使用说明在可信网络上运行。

来源、协议版本与启动方法见[可行性与实现说明](../codex-subscription-feasibility.zh_CN.md)
和[主机中继手册](../../services/codex_gateway/README.zh_CN.md)。
