# 智能 TODO：音频与 NFC 硬件实现和验收

本文件说明 `components/board_services` 的实际能力、接口契约和手机操作步骤。
软件实现和 NDEF 主机测试不代表麦克风、扬声器或手机触碰已通过实机验收。
本次开发没有烧录、读取身份分区、修改 Recovery 或写入物理 NFC 标签。

## 两块板的能力

| 能力 | Vibe Passport / AI Passport | Vibe Note / NOTE4 BLACK-WHITE |
| --- | --- | --- |
| 音频芯片 | ES8311，I²C 0x18，I²S0 | ES8311，I²C 0x18，I²S0 |
| 麦克风输入 | 8 kHz / mono / PCM16 | 8 kHz / mono / PCM16 |
| 扬声器提醒 | 本地双音提示；PA 常通，codec 静音 | 本地双音提示；GPIO46 仅播放时使能 |
| NFC | 独立被动 NTAG213，无 MCU 接口 | 0x55 的 I²C NFC；沿用上游 16 字节块寻址 |
| NFC 配网 | 手机一次写入固定 AP 的 WSC + URI，之后触碰使用 | 配网页自动写 WSC + URI，URL 阶段可改为 URI |
| NFC 自动更新/退出清理 | 固件无法写独立标签；只写无密码固定 AP 信息 | 固件写入并回读用户区，退出改为无密钥固定 URL |
| NFC 与手机系统 | WSC 是否弹出连接由系统决定；URL 后备 | 同左；动态写成功不等于手机支持自动加入 Wi-Fi |

Passport 官方硬件指南明确写明 NTAG213 为被动标签、没有 MCU-facing BSP API；
它不是由 ESP32-C3 的 I²C 控制的外设。其 144 字节用户区足够容纳本项目固定、
开放 AP 的 WSC 和短 URL。NOTE4 的上游公开驱动定义 I²C 地址与块范围，未明确
芯片完整料号；本实现依照已经锁定的板级寻址行为，不通过读 UID 猜测料号。

上游依据：

- [Passport 官方硬件开发指南](https://github.com/FoloToy/ai-passport/blob/main/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)，本地参考文件为 `../reference/ai-passport/docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md` 的 3.1/3.2 节。
- [NTAG213/215/216 数据手册](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)：RF 接口、LA/LB 天线引脚、NTAG213 144 字节用户区。
- NOTE4 已锁定的源码为 `firmware/zectrix/components/zectrix_board/zectrix_nfc.{h,cc}`，参考仓库 `../reference/zectrix-note4-epd-demo`。新生产路径为 `components/board_services/board_nfc_note.cc`，不编译旧 NFC 自测、UID 日志或字段检测任务。
- [NXP NTAG I²C plus 数据手册](https://www.nxp.com/docs/en/data-sheet/NT3H2111_2211.pdf)：说明 I²C/RF 双接口、持久 EEPROM 以及 RF 仲裁。此资料用于协议与断电行为参考，不作为 NOTE4 具体料号已确认的证明。

## 公共接口及线程规则

头文件：`components/board_services/include/board_services.h`。

```c
bool board_ok_is_pressed(void);
esp_err_t board_audio_start_capture(void);
esp_err_t board_audio_read(int16_t *samples, size_t capacity,
                          size_t *read_samples, uint32_t timeout_ms);
void board_audio_stop_capture(void);
esp_err_t board_audio_beep(void);
esp_err_t board_nfc_set_wifi(const char *ssid, const char *password, const char *url);
esp_err_t board_nfc_set_url(const char *url);
void board_nfc_stop(void);
esp_err_t board_nfc_last_error(void);
```

所有音频/NFC I/O 由 `app_controller` 单一工作线程调用。不要在 LVGL、EPD 或
按键回调中调用阻塞 I/O。C3 使用已有 `bsp_i2c_init()`/`bsp_i2c_bus()`；S3 从
`i2c_master_get_bus_handle(I2C_NUM_0)` 取得板级已建立的总线并复用递归 I²C 锁。
不创建新的 ADC unit、不扫描 I²C、不修改共享 ADC 按键或 RTC 初始化。

`board_audio_read()` 的 `capacity` 和 `read_samples` 均以样本计，不是字节。
输出为有符号、原生 little-endian 16 位单声道，固定 8000 样本/秒。底层直接使用
IDF I²S 有限超时读取；`ESP_ERR_TIMEOUT` 时仍可能有有效短数据，应使用实际
`read_samples`。调用者不得将超时当作一整块零填充成功。流式发送可以采用
160 个样本（320 字节、20 毫秒）一块，任何 ASR 网络缓冲也应保持有界。
RX 队列溢出由 I²S ISR 在短 `portMUX` 临界区写入独立标记；后续 read 返回
`ESP_ERR_INVALID_RESPONSE`、零个有效样本，取消这次语音请求，不将丢失中间帧的
录音作为完整指令执行。每次开始录音前清理上一会话的溢出状态。

驱动只在首次捕获/提示音时分配 codec/I²S。每个方向 4 个 DMA 描述符，每帧
160 个样本；初始化阶段是 stereo，codec open 会按单声道左槽重配。
`no_dac_ref=true` 避免把 DAC 参考信号误当麦克风，配置麦克风 PGA 为 30 dB。
停止时关闭 codec 并停止 DMA；下次 open 复用硬件句柄。遇到 I²C 锁失败，仍先
停止独立的 I²S DMA，后续再重建损坏状态，避免松键后继续采集。

普通操作提示由固定正弦查表产生约 700 毫秒的双音。到期提醒使用六个短音：
连续三声、静音 1 秒、再连续三声；每个短音约 140 毫秒，同组相邻短音间隔
约 80 毫秒。结束后静音并关闭 codec。录音期间音频接口返回
`ESP_ERR_INVALID_STATE`，
由 controller 延后提醒播放，避免提示音被录入。该接口不做 TTS、不需要网络。
ESP-IDF 的 I²S read/write 是有超时的 DMA API；详见
[ESP-IDF 5.5.3 I²S 文档](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/peripherals/i2s.html)。
codec 使用上游 Passport 同款 `espressif/esp_codec_dev`，精确锁定 `1.6.2`。

## OK 长按与松开

两板长按阈值仍为 1.5 秒。Passport 新增 `BSP_BTN_RELEASE`，从现有 button
组件 `BUTTON_PRESS_UP` 转发，不新建 ADC。NOTE4 新增 `kRelease`，长按后松开
不会同时生成 click。普通点击保留原行为。

NOTE4 的 `SetOkReleaseCallback(callback, context)` 在按键任务检测到长按 OK
松开时直接调用，之后再排队 `kRelease`。该回调只设置 controller 的停止标记，
使 EPD 正在刷新时也能及时停止语音。注册应在进入正常交互前完成；回调禁止
网络、音频、NFC、显示或阻塞锁操作。
`board_ok_is_pressed()` 直接查询已存在的 Passport 按键驱动状态或 NOTE4 GPIO0，
与逻辑 hold 标志同时检查，避免“release 已处理，排队的 START 后到”重新启动录音。

## NOTE4 动态 NFC 配网

1. 进入配网，controller 将当前开放 AP 的 SSID、空密码和 `http://192.168.4.1/`
   交给 `board_nfc_set_wifi()`。写入第一条 MIME `application/vnd.wfa.wsc` 记录，
   第二条 NFC Forum URI `U` 记录；WSC 中有 Network Index、SSID、Open/None、
   空 Network Key 与广播 MAC，不包含 LLM key、账户 token 或家庭路由器密钥。
2. 手机支持 WSC 时，可触碰并按系统提示加入 AP。系统可能只执行第一条记录，
   不保证同一次触碰还能打开网页；加入后使用屏幕 URL 二维码或再次触碰。
3. controller 可在网页管理阶段调用 `board_nfc_set_url()`，将标签改为单条
   URI（例如设备屏幕展示的当前局域网地址）。手机须已经连上可访问该地址的网络。
4. controller 在退出、取消、超时、失败以及重启后的初始化路径调用
   `board_nfc_stop()`：把用户区改为固定 `http://192.168.4.1/`，清零全部剩余
   用户字节，回读验证，再关闭 GPIO21。它不清空 Wi-Fi NVS 或任何固件分区。

写入范围严格限制在块 `0x01..0x37`（880 字节），不读写块 0、UID、CC、锁位、
配置寄存器或工厂身份区域。更新先写空 NDEF TLV，写完尾部再提交首块，使中途
失败不会发布混合消息。每块先比较，已相同的不写 EEPROM；写后逐块回读确认。
扫描失败不重置共享 I²C、不循环重启外围设备。手机 RF 场占用时返回超时，
controller 可显示二维码后备并在手机移开后重试。

**关掉 NFC GPIO 不能证明内容已清除。** EEPROM 在 MCU 断电后仍可被手机 RF
读取。若清理失败，`board_nfc_last_error()` 返回失败并输出无敏感内容的错误码；
必须显示/记录失败并移开手机重试；失败时保留 NFC 供电，不中断仍在进行的手机
RF 会话，仅成功清理后才降低供电。若设备在配网过程中物理断电，直到下次开机
清理前仍可能保留最后写入的 AP 信息。本产品只向标签写开放 AP 的固定连接信息，
不应向该路径传入家用 Wi-Fi 密码、LLM key 或临时网页认证令牌。

## Passport 一次性手机写 NFC 标签

这条路径可在现有被动标签上完成 NFC 配网，但第一次必须由手机写入；固件无法
代写、更新或回读标签。AP SSID 来自屏幕，正常重启保持不变；恢复出厂或设备标识
变化后，需要重新写入标签。固定 AP URL 不会追随 STA 的动态 `192.168.x.x` 地址。

生成对应字段和开发者用 NDEF 二进制：

```bash
python3 tools/nfc-setup.py \
  --ssid VibePassport-XXXX \
  --output /tmp/passport-wifi.ndef
```

把 `VibePassport-XXXX` 换成**设备实际配网页显示的完整 SSID**。工具只接受开放
AP，不接受密码；输出 `.ndef` 消息和 `.records.json` 字段清单，计算 Type 2 TLV
总长度并拒绝超过 NTAG213 144 字节容量的数据。示例 `VibePassport-DEMO` 的
WSC + URI 是 104 字节 NDEF、107 字节用户区；最长 32 字节 SSID 也可容纳。
二进制文件不是 flash 镜像或 UID dump，生成文件不等于已经写入物理标签。

建议用 Android 上的 NFC Tools 完成以下操作（菜单名可随语言/版本略有差异）：

1. 在「读取 / Read」页触碰 Passport 背面标签，确认可写和剩余容量；如原来有
   联系方式或官方入口，先在手机中保存需要保留的内容，不要把 UID 截图/导出
   到项目。NDEF 用户内容与 ESP32 工厂身份分区是不同存储。
2. 在「写入 / Write」页，选择「添加记录 / Add a record」→「Wi-Fi 网络」。
   SSID 填屏幕上的实际 AP 名称，认证选 Open / None，密码留空。
3. 再添加「网址 / URL」记录：`http://192.168.4.1/`。让 Wi-Fi 在第一条、URL 在
   第二条，然后一次写入这两条记录；选择标准数据记录，不使用 Tasks、外部脚本
   或启动应用记录，不设置永久只读锁。此操作替换标签原有 NDEF 用户内容。
4. 返回读取页再次触碰，确认 Wi-Fi 的 SSID、开放网络、空密码和 URL 两条记录。
   手机软件生成的可选属性可能增加字节数；如它报告容量不足，删除多余标题/
   描述字段，或使用单条 Wi-Fi 记录并以屏幕 URL 二维码打开门户。
5. 退出写卡 App，将 Passport 保持在配网页；手机解锁并打开 NFC 后重新触碰。
   若系统出现加入 AP 提示，确认后留在无互联网的设备热点。用屏幕 URL 二维码
   打开门户，或使用 NFC App 读取并打开 URI。实际是否自动弹出 Wi-Fi 提示必须
   在目标手机验收，不能从 NDEF 字节有效推断。

[NFC Tools 官方说明](https://www.wakdev.com/en/apps/nfc-tools-android.html)
明确支持标准 Wi-Fi、URL 和多个记录。`.ndef` 输出供支持原始 NDEF 的开发工具
导入；不声称任意手机 App 都能直接导入该文件。NXP TagWriter 的官方备份导入
格式是 `.twdb`，见其 [使用手册](https://inspire.nxp.com/tagwriter/tag-writer-user-manual.pdf)。
TagWriter 也支持在 UI 新建 Wi-Fi dataset，但不应把“Write Multiple”批量写多个
标签误认为向同一标签添加 Wi-Fi + URL。

iPhone 的后台读取检查 NDEF URI 记录，并不承诺执行 WSC 加网；见
[Apple 后台读标签文档](https://developer.apple.com/documentation/corenfc/adding-support-for-background-tag-reading)。
在 iPhone 上先用屏幕 Wi-Fi 二维码/系统设置加入 AP，再触碰 URL；设备支持、
锁屏、NFC 使用状态均可能影响提示。若只需要这一用法，可只写固定 URL 到标签，
之后每次先连接 AP 再触碰。Android 也可能受厂商系统限制而需要相同后备流程。

## 验证与明日实机清单

已完成的独立 NDEF 主机检查：

```bash
python3 components/board_services/tests/test_ndef.py
python3 tools/nfc-setup.py --ssid VibePassport-DEMO --output /tmp/vibe-passport-demo.ndef
```

6 项用例通过：独立解析 C 输出的 WSC 与 URI、UTF-8 SSID、32 字节 SSID + 63
字节测试密码上界、无越界截断、254/255 字节 TLV 边界、Python 静态导出和固件
C 编码字节一致。所有测试数据为合成内容；不访问设备或真实凭据。

需在每块板、指定手机和最终固件上记录以下结果。每条记录带源码 revision、
镜像 SHA256、板名、chip/flash/PSRAM、电源来源、操作、期望、观察和脱敏证据。

| 验收项 | 操作与期望 | 当前证据 |
| --- | --- | --- |
| 双板编译和镜像边界 | 根目录 `./tools/validate.sh --firmware`；3 MiB Passport app / 身份 / Recovery 边界通过 | 由总集成验证记录；本文件不替代结果 |
| 麦克风 | TODO 页长按 OK；正常语音、静音、噪声各一次，识别文本可用，无恒零/参考音 | 未做 Device / Real ASR |
| 松键 | NOTE4 刷新中松开；controller 停止录音，无长按后多余 click | 代码路径已接，未做 Device |
| 重复录音 | 连续开始/停止至少 30 次；没有 I²S channel leak、音调倍速或堆下降 | 未做 Device |
| 提示音 | 两板用短到期 TODO 听到双音；录音时提醒延后，结束后播放 | 未做 Device |
| NOTE4 NFC | 配网页写后手机读到 WSC + URI；手机号/UID/密码不进日志 | 字节 Host PASS；Device 未做 |
| NFC 退出 | 退出配网页后手机读回只有固定 URL；后续用户字节不含旧 SSID/key | driver 逐块回读；Device 未做 |
| NFC 场冲突 | 手机贴住时更新；错误可见，不重启/死锁；移开重试成功 | 未做 Device |
| Passport 一次写卡 | 手机写 WSC + URI 后，重启相同 AP 可触碰；不支持 WSC 手机能扫码后触碰 URL | 导出容量 Host PASS；手机写卡未做 |
| 掉电与恢复 | 配网时断电；下次开机清理后只读固定 URL；工厂身份/Recovery 不变 | 未做 Device / Installer |
| 内存与电源 | C3 无 PSRAM 情况下语音+Wi-Fi不中断；提醒时保持可用电源，不默认依赖深睡唤醒 | 未做 Device / Power |

Build、Host、CI、Real API、Device、Installer、Power 各自记录，不得将 Host 编码
通过标为手机感应配网通过。完整应用验收还须覆盖真实 ASR/LLM、TODO 增删改、
持久化、相对时间/周期提醒和设置页配置流程，见智能 TODO 总设计与验收文档。
