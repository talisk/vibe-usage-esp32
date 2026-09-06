# Codex 订阅用于语音 TODO

核查日期：2026-09-06。本记录取代早先“尚未证明 ASR 可用”的调查结论。

**ASR 和 LLM 均可通过在线主机上的实验性中继使用现有 ChatGPT 订阅登录。**
本机已经用官方 Codex CLI 0.153.3、无 API Key 的 ChatGPT 登录，完成合成
语音的 WebRTC v3 用户最终转写。固件配置页和中继实现已加入仓库；具体
运行结果、构建产物和未验收项目见[本次验收记录](acceptance/2026-09-06-codex-subscription.md)。

这不是把订阅令牌填入 Platform API，也不是 ESP32 直接执行 OAuth。
设备连接局域网中继；官方 Codex 客户端在主机上管理登录和续期。配置步骤见
[中继说明](../services/codex_gateway/README.zh_CN.md)。

## 授权和计费边界

Codex 的 ChatGPT 登录和 Platform API Key 是不同的授权路径。个人订阅
登录凭据不能直接替换 `api.openai.com/v1/audio/transcriptions` 的 API Key。
[官方身份验证](https://learn.chatgpt.com/docs/auth)、
[文件转写 API](https://developers.openai.com/api/docs/guides/speech-to-text)。

本实现的 LLM 走官方 App Server 的订阅模型任务；ASR 则打开实验性 Voice
会话，播放设备录音并提取用户的最终 transcript。**Voice 与 Codex 任务
各有用量限制**，不能将两者称为同一份 Codex 额度，也不能承诺免费或无限。
实际权限取决于当前登录账户和服务端状态。
[Voice 访问及限制](https://learn.chatgpt.com/docs/features/voice)。

App Server 的相关接口具有实验性质；本项目锁定已核查的 CLI 版本，升级
后需要重新验证。它是个人设备的实验性适配，不是官方保证的通用 ASR 服务。
[App Server](https://learn.chatgpt.com/docs/app-server)。

## 实际接入路径

```text
ESP32 麦克风 → 8 kHz PCM16 WAV → 私有 HTTP 中继
  ASR: Codex App Server → Voice WebRTC v3 → 最终用户 transcript
  LLM: Codex App Server → 订阅模型 → 严格 TODO 操作 JSON
ESP32 本地动作校验 → NVS 提交 → 待办页面
```

| 配置 | 值 |
| --- | --- |
| 配置页接入方式 | Codex subscription（实验性主机中继） |
| 中继地址示例 | `http://192.168.1.10:8765` |
| Chat 路由 / 模型别名 | `/v1/chat/completions` / `codex-default` |
| ASR 路由 / 模型别名 | `/v1/audio/transcriptions` / `codex-voice` |
| 设备 Key | 单独生成的中继 Token；不是 ChatGPT 或 Codex 账户凭据 |

主机必须持续在线，并能访问订阅服务；电脑休眠时设备不能完成语音请求。
`codex-default` 是本地路由别名，实际 LLM 模型使用官方客户端的默认值，或
由中继 `--model` 明确选择。`codex-voice` 也是别名，不是 Platform 模型 ID。
设备配置结构和已保存的 TODO 数据不需要迁移。

## 为什么选择 WebRTC v3

对照 OpenAI 官方 `rust-v0.153.3` 源码：

- Realtime WebSocket 路径要求 API Key，不能从当前 ChatGPT 登录直接获得。
- WebRTC 路径可复用 ChatGPT 登录，但只支持 conversational v1/v3；不支持
  transcription-only，纯文本输出又要求 v2。因此需要主机进行音频协议适配。
- 实测 v1 被服务端以 quicksilver 版本不匹配拒绝；v3 则成功完成连接和
  合成录音最终转写。v1 的失败不能解释为账户不支持所有订阅语音。

[WebSocket 认证](https://github.com/openai/codex/blob/rust-v0.153.3/codex-rs/core/src/realtime_conversation.rs#L1261)、
[WebRTC 限制](https://github.com/openai/codex/blob/rust-v0.153.3/codex-rs/core/src/realtime_conversation.rs#L1313)、
[输出模式限制](https://github.com/openai/codex/blob/rust-v0.153.3/codex-rs/core/src/realtime_conversation.rs#L1418)。

该源文件 SHA-256：
`3b113647331fb553349a79bf661f5899a4576364ef3b6181eabe0bbc84c9fc82`。

## 转写完整性与失败行为

录音完整播放后，中继继续发送四秒静音，再关闭会话。v3 没有暴露音频
commit RPC；这段静音是经验等待窗口，不能证明任意网络条件下服务端已经
处理全部音频。中继只接受 App Server 的最终用户文本，丢弃助手回答及
所有部分转写；已观察到的后续片段未完成、前后文本不一致、超时或发生
任务委派时均失败，设备保持原待办。

实际测试发现，“完全保持沉默”的会话提示可能只有 delta、没有最终事件。
因此中继允许 Voice 简短确认，但既不播放也不返回助手音频/文字。模型仍
可能识别错误；当前验证不能替代真实麦克风、口音、噪声和句尾停顿测试。

每次请求使用独立进程和临时会话，禁用工具、MCP、插件及工作区能力；
遇到意外 Codex 任务会中断并拒绝结果。中继不保存录音、转写或 TODO
内容，不读取/导出登录凭据；账户登录存储与刷新仍由官方客户端负责。
音频会发送给订阅服务，不能将“主机不落盘”理解为服务端不处理或不保留。

## 验证边界

已完成的服务端实测只使用合成语音和虚构待办，不含麦克风录音或个人待办。
详细证据保存在[订阅接入验收](acceptance/2026-09-06-codex-subscription.md)。
两款真实设备的 Wi-Fi 可达性、音频效果、堆内存和耗电尚待验收；未刷写设备。
