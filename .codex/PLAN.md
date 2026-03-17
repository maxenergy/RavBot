# RavBot Android 具身助手 MVP 规划

## Summary
- 目标形态已经锁定：`原生 Android App`、`全端侧`、`Android 12+`、`高通 Snapdragon 8 Gen 2/3/8 Elite` 优先、`中英双语语音`、`前台连续视觉理解`、`轻量 2D 表情脸`。
- RavBot 当前缺口很明确：仓库里没有 Android 宿主，没有真实的 `audio/talk/canvas` 运行时，没有移动端媒体能力，`talk-voice` 还是占位，现有 Web UI 的 mobile 只是响应式布局，不是手机原生能力。
- MVP 主路线定为：`RavBot C++ core + Android JNI 宿主 + llama.cpp(Vulkan) + sherpa-onnx`。`Qualcomm Genie/QNN` 不作为 MVP 主路径，因为截至 `2026-03-17`，Qualcomm AI Hub 的 Android `ChatApp` README 仍要求 `Android 15+`/较新的 vendor meta-build，和你的 `Android 12+` 目标不匹配。
- 参考项目的落点：`RCLI` 提供的是 `VAD/STT/LLM/TTS/VLM/RAG` 并发流水线思路；`qwen3.5-0.8-app` 证明了 `Android + llama.cpp + JNI + mmproj + Vulkan` 的打包方式可行。

## Key Changes
- 新增 `ravbot_mobile_core`，从现有 `ravbot_core` 拆出可在 NDK 下编译的移动子集，只保留 `agent/session/prompt/memory/provider/tool-profile` 必需模块；不把 `CLI/Web server/desktop channels/browser/bash/process/sidecar runtime` 带进 Android 构建。
- 新增 `DeviceCapabilityBridge` 抽象，统一 RavBot core 对 `麦克风/扬声器/摄像头/振动/前后台生命周期/模型目录` 的访问；Android 是第一个实现，后续 iOS/平板复用同一边界。
- 新增原生 `android/` App，技术栈定为 `Kotlin + Jetpack Compose + CameraX + AudioRecord + AudioTrack + Foreground Service + NDK/JNI`；不走 WebView/PWA。
- 新增 `LlamaCppMobileProvider`，作为 RavBot 的本地 LLM/VLM provider；默认文本模型定为 `Qwen3.5-0.8B Q4_K_M GGUF`，默认视觉模型定为 `SmolVLM-500M-Instruct GGUF + mmproj`，统一走 `llama.cpp`，默认开启 `Vulkan`，GPU 不可用时自动回退 CPU。
- 新增 `SpeechPipeline`，语音栈统一用 `sherpa-onnx`；STT 选 `SenseVoiceSmall` 级别的中英多语模型，VAD 用 `Silero VAD`，TTS 用 `kokoro-multi-lang-v1_1`。MVP 支持 `边听边转写 + 句级 TTS + barge-in 打断`。
- 语音交互默认行为定为：`前台自动收音 + VAD 自动断句 + 点击说话入口保留`；`唤醒词` 不进入 MVP。
- 视觉默认行为定为：`前台连续理解，但低频采样`，默认 `0.5 fps` 语义采样，只有场景变化达到阈值才触发 VLM；不是 30fps 实时视频推理。默认不保存原始视频帧，只保存 `场景摘要`，除非用户显式拍照留存。
- 头像默认行为定为：`自绘 2D 脸部状态机`，状态固定为 `idle/listen/think/speak/watch/error`；口型由播放 PCM 振幅驱动，眨眼和表情切换由状态机驱动，不引入 Live2D/Rive。
- Memory 方案定为：MVP 先复用 RavBot 现有 `session history + structured memory + BM25/FTS`，把 `语音转写、回复、场景摘要` 都写入本地存储；Android 第一版默认 `关闭向量召回`，因为现有本地 embedding 实现对中文不够可靠，移动端多语 embedding provider 作为下一阶段补齐。
- Tool 方案定为：新增 `mobile_safe` 工具档，只保留 `memory/search/web/time/camera snapshot/device status` 这类移动端安全能力；显式禁用 `bash/browser/apply_patch/process` 等桌面工具。
- 事件协议定为：Android 不内嵌 HTTP/WebSocket server，但 JNI 回调的事件名和现有 gateway 语义对齐；保留 `assistant_delta/assistant_final/tool_start/tool_result`，新增 `mobile.asr_partial/mobile.asr_final/mobile.tts_state/mobile.avatar_state/mobile.vision_observation`。

## Public Interfaces
- 新增配置段：`mobile.runtime`、`mobile.models`、`mobile.audio`、`mobile.vision`、`mobile.avatar`。
- 新增 JNI/C API：`init_engine`、`start_session`、`send_text_turn`、`push_pcm16`、`push_camera_frame`、`interrupt_generation`、`set_foreground_state`、`subscribe_events`。
- 新增 provider 类型：`llama.cpp` 本地 LLM/VLM provider、`mobile_speech` provider facade。
- 新增移动事件族：`mobile.asr.*`、`mobile.tts.*`、`mobile.avatar.*`、`mobile.vision.*`。

## Test Plan
- C++ 单测覆盖 `mobile provider`、`tool-profile gating`、`config parsing`、`event bridge`、`session persistence`。
- Android instrumentation 覆盖 `权限流`、`模型下载/断点续传`、`JNI 生命周期`、`前后台切换`、`进程重建后会话恢复`。
- 端到端硬件验证至少覆盖 `1 台 Snapdragon 8 Gen 2` 和 `1 台 Snapdragon 8 Gen 3/8 Elite`，系统版本都在 `Android 12+`。
- 验收场景固定为：中文和英文都能听懂并回复；回复过程中能被新语音打断；头像在 `听/想/说/看` 状态下正确切换；连续视觉模式能稳定输出场景摘要；App 重启后历史和记忆可恢复；未授权时不落盘原始摄像头帧。
- 性能门槛固定为：无 ANR、单 session 同时只允许一个推理任务、热降频时自动降视觉采样频率、Vulkan 不可用时能自动回退并继续可用。

## Assumptions And Defaults
- MVP 是 Android 原生客户端，不是浏览器版控制台延伸。
- MVP 是全端侧主模式，不依赖远端 RavBot gateway 才能工作。
- 连续视觉仅限前台，后台持续摄像头处理不做。
- 唤醒词、后台常驻语音守护、Live2D/Rive 角色、Qualcomm Genie/QNN NPU 加速，都明确放到后续阶段。
- Android 第一版的“记忆”先以 `session + structured memory + BM25` 为核心，可用优先；移动端多语向量记忆后补。
- 文本大脑基线模型固定为 `Qwen3.5-0.8B`，不是更大的 1.5B/3B；先保证延迟、功耗和包体可控，再做高阶机型升级档。

## Research Inputs
- RCLI: https://github.com/RunanywhereAI/RCLI
- qwen3.5-0.8-app: https://github.com/jingax/qwen3.5-0.8-app
- Qualcomm AI Hub Apps: https://github.com/quic/ai-hub-apps
- Qualcomm Android ChatApp: https://github.com/quic/ai-hub-apps/tree/main/apps/android/ChatApp
- WhisperKitAndroid: https://github.com/argmaxinc/WhisperKitAndroid
- sherpa-onnx Android docs: https://k2-fsa.github.io/sherpa/onnx/android/index.html
- sherpa-onnx VAD docs: https://k2-fsa.github.io/sherpa/onnx/vad/index.html
- sherpa-onnx KWS docs: https://k2-fsa.github.io/sherpa/onnx/kws/index.html
- sherpa-onnx TTS docs: https://k2-fsa.github.io/sherpa/onnx/tts/index.html
