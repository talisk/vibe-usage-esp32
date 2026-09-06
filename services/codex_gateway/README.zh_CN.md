# Codex 订阅网关

这个电脑端程序为 Vibe Passport 和 Vibe Note 提供两条兼容接口：录音转写和
TODO 操作解析。两条链路均通过电脑上已登录 ChatGPT 的官方 Codex 客户端访问
服务；ESP32 只保存本地网关专用 Key。

语音链路使用 Codex `0.153.3` 的实验性 realtime v3 WebRTC 会话，并提取最终
用户转写。它不是 Platform 的通用文件转写 API。Voice 和 Codex 各有订阅用量
限制，账户或服务端更新可能改变可用性；某次成功不代表所有订阅计划都有同样
权限。语音会话可能生成简短应答，网关丢弃助手文字和音频，只将用户转写交给
后续 TODO 解析。

接口调查、实测范围和限制见
[Codex 订阅可行性](../../docs/codex-subscription-feasibility.zh_CN.md)。
设备端操作见[语音 TODO 用户指南](../../docs/smart-todo-user-guide.zh_CN.md)。

## 1. 准备电脑环境

需要一台与设备处于同一局域网、能够访问对应在线服务的 macOS 或 Linux 电脑。
使用期间让电脑保持开机、联网，并让下面的网关进程持续运行。此程序不安装
开机服务；关闭终端进程或电脑休眠后，设备无法使用这两条服务。

需要 Python 3.11 或更新版本，建议使用经过验证的 Python 3.12；官方 Codex CLI
必须为 `0.153.3`。以下命令均在本仓库根目录执行：

```bash
python3.12 --version
codex --version
codex login status
```

版本输出应为 `codex-cli 0.153.3`。尚未登录时，执行下面的命令并在官方登录
页面使用拥有相应访问权限的 ChatGPT 账户完成登录：

```bash
codex login
```

无桌面浏览器的 NAS、树莓派或远程 Linux 主机可使用设备授权码流程：

```bash
codex login --device-auth
```

在手机或另一台电脑的浏览器中按提示输入授权码。授权码只完成首次登录；
登录凭据刷新和每次请求仍由持续运行的 Codex CLI 与本网关负责。

网关会再次检查当前身份是否为 ChatGPT 登录。它不回退到 API Key 认证，也不
复制、读取或展示 `auth.json` 中的账户令牌；登录和续期由官方 Codex 进程处理。
如果已有 `CODEX_HOME` 环境变量，登录和运行网关应使用同一个既有配置目录。

网关只接受默认 OpenAI 服务来源，并拒绝激活的自定义 provider、profile 或
自定义 ChatGPT/realtime 服务地址。遇到配置拒绝时，在电脑上核对已有 Codex
配置；网关不会修改该配置或将账户凭据迁移到其他目录。

## 2. 安装隔离的 Python 依赖

下面的虚拟环境位于项目之外。安装使用仓库固定的 `aiohttp`、`aiortc` 和 `av` 版本：

```bash
python3.12 -m venv "$HOME/.local/share/vibe-codex-gateway/venv"
"$HOME/.local/share/vibe-codex-gateway/venv/bin/python" -m pip install \
  -r services/codex_gateway/requirements.txt
```

安装完成后，可运行不需要登录、不调用真实模型的 Host 测试：

```bash
VIBE_GATEWAY_PYTHON="$HOME/.local/share/vibe-codex-gateway/venv/bin/python" \
  ./tools/test-codex-gateway.sh
```

## 3. 创建专用 Key 并启动

Key 是网关生成的随机字符串，与 ChatGPT 账户令牌和 Platform API Key 无关。
先建立仅当前用户可访问的目录：

```bash
mkdir -p "$HOME/.config/vibe-codex-gateway"
chmod 700 "$HOME/.config/vibe-codex-gateway"
```

如果只在电脑上测试，省略 `--host`，网关默认监听 `127.0.0.1:8765`：

```bash
"$HOME/.local/share/vibe-codex-gateway/venv/bin/python" \
  -m services.codex_gateway.gateway \
  --token-file "$HOME/.config/vibe-codex-gateway/token" \
  --create-token
```

`--create-token` 仅在文件不存在时生成随机 Key；再次启动会保留已有 Key。
文件必须是当前用户拥有的普通文件，权限严格为 `0600`，不能使用符号链接或
多重硬链接。程序不会把 Key 输出到终端。

**`127.0.0.1` 只能由电脑自身访问，设备不能使用这个地址。** 给设备使用时，先
按 `Ctrl+C` 停止测试进程，然后将下面示例中的 `192.168.1.10` 换成这台电脑
当前网络接口的 IPv4 地址：

```bash
"$HOME/.local/share/vibe-codex-gateway/venv/bin/python" \
  -m services.codex_gateway.gateway \
  --host 192.168.1.10 \
  --port 8765 \
  --token-file "$HOME/.config/vibe-codex-gateway/token"
```

网关只接受回环或 RFC1918 私有 IPv4 字面量：`10.x.x.x`、`172.16.x.x` 至
`172.31.x.x`、`192.168.x.x`。不接受 `0.0.0.0`、公网地址、域名或 IPv6。
该地址必须实际属于本机；监听其他设备的地址会失败。电脑防火墙应允许设备
所在局域网访问所选端口。

设备到网关使用局域网 HTTP，录音、任务内容和网关 Key 会经过这段网络。
请在自己信任的局域网中使用，不要配置路由器公网端口转发。网关到在线服务
仍由官方客户端和 WebRTC 使用各自的安全传输。

## 4. 在设备配置页选择 Codex 订阅

1. 在设备 Settings 中打开 LLM 配置，按屏幕提示完成 Wi-Fi 连接并扫描局域网
   配置页二维码。
2. 在“服务方式 / Service preset”选择
   “Codex subscription (host gateway, experimental)”。
3. “电脑的局域网地址”填写启动时使用的地址，例如
   `http://192.168.1.10:8765`。只填写协议、IPv4 和端口，不加接口路径。
4. 将网关 token 文件里的字符串粘贴到网关 Key 字段，勾选启用并保存。

macOS 可用以下命令将 Key 复制到剪贴板，再粘贴到设备配置页；命令不会打印
文件内容。其他系统可在本机编辑器中打开 token 文件并复制。

```bash
pbcopy < "$HOME/.config/vibe-codex-gateway/token"
```

无需把 Key 发送到聊天或写进仓库。配置页不会读回已经保存的 Key；同一网关
已有两项 Key 时，留空可保留。切换服务方式或更换网关地址时，需要重新填写
目标服务的 Key。选择预设本身不会保存，点击 Save 后才会提交。

Codex 预设会同时配置以下两项，手动配置时也必须保持这些固定别名：

| 字段 | 上述示例对应值 |
| --- | --- |
| Chat Endpoint | `http://192.168.1.10:8765/v1/chat/completions` |
| Chat model | `codex-default` |
| ASR Endpoint | `http://192.168.1.10:8765/v1/audio/transcriptions` |
| ASR model | `codex-voice` |
| 两项 Key | 同一个本地网关 Key |

这两条路由属于本项目网关。不能将它们替换为 Codex App Server 的 JSON-RPC
地址，也不能把订阅账户令牌填入设备 Key 字段。

## 5. 模型选择与运行方式

设备上的 `codex-default` 是固定别名，实际 LLM 模型默认继承电脑 Codex 配置。
如果需要选择该订阅可用的其他模型，在电脑端启动命令后增加 `--model` 和实际
模型名称。别名 `codex-default` 保持不变；`--model` 只控制 TODO 解析所用的
LLM，不改变 Voice 服务的模型。

如果 CLI 不在 `PATH` 中，可通过 `--codex` 指定官方可执行文件路径。启动时会
校验 CLI 版本；更高版本不会自动视为兼容，需要先验证对应实验性协议。

每个请求都会启动新的官方 App Server 进程，使用空工作目录和 ephemeral
会话。环境、文件工具、MCP、插件、应用、网页检索及多代理能力被禁用；运行时
还会检查 MCP 状态，拒绝工具请求，并中断非预期的后台任务。

网关同时只处理一个操作。另一块板同时请求时会收到忙碌错误；当前请求结束
后可重新发起。客户端断开、超时或 `Ctrl+C` 停止服务都会取消请求，终止对应
进程组并清理临时工作目录。

## 请求范围、超时与错误

这个网关仅接受当前固件的有限请求结构，不能作为通用 Chat Completions
代理。Chat 请求必须含有合法 TODO 数据；ASR 必须为最长八秒的 8 kHz、单声道、
PCM16 WAV，并使用 `response_format=json`。网关不接受任意工具或模型参数。

| 限制 | 当前值 |
| --- | --- |
| Chat 请求体 | 最多 16,384 字节 |
| ASR multipart 请求体 | 最多 140,000 字节 |
| 请求体读取期限 | 12 秒 |
| 身份与配置检查期限 | 3 秒 |
| 读完请求后的 Chat / ASR 总期限 | 25 秒 / 32 秒 |
| 返回用户转写 | 最多 512 个 UTF-8 字节 |
| 返回 TODO 操作 | 校验后与固件 action schema 一致 |

身份与配置通过检查后，网关每秒发送一个 JSON 空白字符，让固件的五秒 socket
读取超时保持活跃，完成后再发送一个完整 JSON 值。响应使用 HTTP chunked
编码；它不是 SSE，设备也不应将空白视为成功。

发送响应头前发现的错误具有相应 HTTP 状态。如果错误发生在心跳已经开始
之后，状态仍是 HTTP 200，最终正文会是 `error` 对象，不包含成功的 `text`
或 `choices`。固件会拒绝这种响应并保留原 TODO 状态。

| 错误 | 含义和处理 |
| --- | --- |
| `invalid_gateway_token` / 401 | Key 缺失或不匹配；核对设备两项 Key 与本地文件。 |
| `subscription_login_required` / 401 | 电脑 Codex 未以 ChatGPT 登录，或登录需要恢复；在电脑运行 `codex login`。 |
| `subscription_access_denied` / 403 | 当前账户不能访问该服务；核对订阅权限。 |
| `gateway_busy` / 429 | 当前操作尚未结束；结束后再试。 |
| `subscription_limit` / 429 | 服务报告订阅用量限制；Voice 与 Codex 额度分别核对。 |
| `codex_version_unsupported` | 本地 CLI 版本与已验证协议不符，网关未启动。 |
| `codex_configuration_unsupported` / 503 | 存在不支持的 provider、profile 或服务地址配置。 |
| `codex_tools_not_disabled` / 503 | 未能证明工具已禁用，请求被拒绝。 |
| `unsupported_chat_request`、`invalid_todo_input`、`invalid_audio_request` / 400 | 请求格式或固定别名不符；核对固件和配置。 |
| `request_too_large` / 413 | 请求超过大小上限。 |
| `subscription_timeout` / 504 | 身份检查或请求处理超时；检查电脑与在线服务连接。 |
| `voice_*`、`invalid_model_result` 或其他 502 | 转写未完整结束、触发非预期任务、输出无法校验或上游请求失败。此次操作不保存。 |

部分上游错误只能安全归类为一般失败，网关不会把包含账户信息或服务端原文
的错误消息返回设备。

## 数据与验收边界

音频和转写只在网关进程内存中处理，不写入文件；临时目录仅用作空工作目录。
网关关闭 HTTP access log，不记录请求体、Key、音频、转写、TODO 标题、账户
详情或服务端原始错误。每次调用的 Codex 会话不持久保存对话历史。

执行语音命令时，录音会发送给在线 Voice 服务，最终用户转写和当前 TODO 列表
会发送给在线 LLM。电脑上的官方客户端仍会按其自身机制维护登录状态；本项目
的“不写入音频”不等于在线服务不处理或留存请求数据。

Voice v3 没有独立的“音频提交完成”接口。网关会播放完整录音，等待静音尾部，
只接受已观察到的完整最终用户片段；未完成片段、冲突转写或非预期后台任务会
使请求失败。这不能代替真实麦克风、不同口音及较差网络下的验收。

Host 测试不调用真实订阅；真实服务结果、双板固件构建、设备操作、安装恢复和
电源行为必须分别记录。当前证据见
[Codex 订阅验收记录](../../docs/acceptance/2026-09-06-codex-subscription.md)。
