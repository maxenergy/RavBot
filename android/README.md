# RavBot Android Host Scaffold

This directory is a standalone native Android host scaffold for the RavBot
embodied assistant MVP. It is intentionally limited to the Android shell:

- Gradle project structure for a single `app` module
- Compose-based host UI with a lightweight 2D avatar/state surface
- Kotlin JNI bridge APIs wired to the embedded `ravbot_mobile_core`
- Runtime permission handling and a foreground service shell
- Foreground service notification with `Open` / `Stop` actions plus live
  session, capture, microphone, camera, speaker, and assistant status
- `AudioRecord` microphone capture with simple silence-based turn detection
- Active speech turn flush when capture stops or the host backgrounds, so
  partially spoken input is promoted to a final ASR turn instead of being lost
- `CameraX` image analysis feeding compact luma frames into the mobile core
- Android `TextToSpeech` playback wired to `mobile.tts_state` events
- Host snapshot persistence plus best-effort restore of the last
  `engine/session/service/capture` runtime shape after app relaunch
- Host runtime status mirrored back into native as `mobile.device_status`
  so UI and future device-safe tools share one source of truth
- `device_status` tool results now include both the last published host
  snapshot and the current `mobile.runtime_status` readiness payload, so the
  local model can inspect provider/backend/web/haptics capability state
  directly
- A dedicated `runtime_status` tool now exposes that readiness payload on its
  own for local-model tool calls
- Android host haptics are now bridged into native readiness, exposed as a
  `vibrate` mobile-safe tool when the device supports vibration, and can be
  toggled on or off from the host UI/runtime service state
- Native `modelsDir` resolution for relative mobile model paths
- `mobile.runtime_status` diagnostics for provider/backend/model readiness
- Compose status cards for live `assistant_delta`/`assistant_final` streaming
  state and mobile tool activity
- A first native `mobile_safe` tool path: local models can call
  `device_status`, `runtime_status`, `vibrate`, and `camera_snapshot`, receive
  the latest host/device
  snapshots, use host-backed `web_search` / `web_fetch`, plus `time` and
  `memory_list` / `memory_search` / `memory_get` / `memory_write` /
  `memory_delete` against the mobile workspace, and continue generation with
  the tool results persisted in session history
- `camera_snapshot` now stays scoped to the active mobile session and reports
  freshness metadata such as `ageMs`, `stale`, and
  `capturedWhileForeground`
- Native vision sampling cadence and scene-change throttling are now also
  scoped per mobile session, so one conversation's camera frames do not
  suppress another session's first observation
- Session-bound mobile events now carry `sessionKey`, and the Android host
  ignores stale event traffic from older sessions when a new session is active
- `camera_snapshot` also carries the latest published `deviceStatus`, so the
  local model can tell whether the camera is currently running or whether it
  is looking at an older observation
- `camera_snapshot` now includes `visionProvider` metadata plus structured
  unavailable reasons such as `background_gated`, `capture_not_requested`,
  and `camera_not_running`
- Android CameraX frames now carry a capture-time-derived epoch timestamp into
  native `camera_snapshot` results instead of using only JNI receive time
- Avatar mouth motion driven by microphone level while listening and by a
  synthetic speaking envelope while Android `TextToSpeech` is active
- Native `SpeechPipeline` facade for STT/TTS asset readiness and placeholder ASR
- Local placeholder LLM/ASR/vision providers so the host can exercise the
  mobile event loop before llama.cpp and sherpa-onnx are linked

What it does not do yet:

- Ship model runtimes such as `llama.cpp` or `sherpa-onnx`
- Replace Android host TTS plus the placeholder ASR/VLM/TTS providers with
  real on-device sherpa-onnx / llama.cpp model runtimes
- Persist or replay raw camera/audio media beyond the active session

How it fits the repo:

- The Android app is the future device host for `ravbot_mobile_core`
- `RavbotNativeBridge` mirrors the planned mobile engine surface:
  `initEngine`, `startSession`, `sendTextTurn`, `pushPcm16`,
  `pushCameraFrame`, `interruptGeneration`, `setForegroundState`,
  `subscribeEvents`
- The native layer now embeds the mobile core directly; the remaining gap is
  replacing placeholder providers with real llama.cpp and sherpa-onnx
- `MobileEngine` now enforces the configured vision sampling cadence and
  scene-change threshold, so different hosts have a shared native fallback
- The current local llama.cpp provider now reports whether the backend is
  linked and whether text/vision model files exist under the configured
  `modelsDir`
- The current speech pipeline does the same for `sttModel` and `ttsVoice`,
  so the Android host can tell whether sherpa-onnx assets are present before
  the real backend is linked

Open `android/` as a separate Android Studio project. The scaffold targets
Android 12+ (`minSdk 31`) and is biased toward `arm64-v8a` devices.
