package com.ravbot.android.ui

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.ravbot.android.bridge.NativeEvent
import com.ravbot.android.bridge.RavbotNativeBridge
import com.ravbot.android.media.AudioCaptureController
import com.ravbot.android.media.CameraFrameController
import com.ravbot.android.media.SpeechPlaybackController
import com.ravbot.android.service.RavbotForegroundService
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import org.json.JSONObject
import kotlinx.coroutines.delay
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner

private val ravbotColorScheme =
    lightColorScheme(
        primary = Color(0xFF005F73),
        secondary = Color(0xFF0A9396),
        tertiary = Color(0xFFCA6702),
        surface = Color(0xFFFFFBF3),
        background = Color(0xFFF7F4EA),
        onPrimary = Color(0xFFFFFFFF),
        onSurface = Color(0xFF1C1B1A),
    )

private enum class AvatarState(val label: String) {
  Idle("idle"),
  Listen("listen"),
  Think("think"),
  Speak("speak"),
  Watch("watch"),
  Error("error"),
}

@Composable
fun RavbotApp() {
  MaterialTheme(colorScheme = ravbotColorScheme) {
    Surface(color = MaterialTheme.colorScheme.background) {
      RavbotHostScreen()
    }
  }
}

@Composable
private fun RavbotHostScreen() {
  val context = LocalContext.current
  val appContext = context.applicationContext
  val lifecycleOwner = LocalLifecycleOwner.current
  val bridge = remember { RavbotNativeBridge() }
  val snapshotStore = remember(appContext) { HostSnapshotStore(appContext) }
  val restoredSnapshot = remember(snapshotStore) { snapshotStore.load() }
  val logEntries = remember { mutableStateListOf<String>() }
  var avatarState by rememberSaveable {
    mutableStateOf(
        AvatarState.entries.firstOrNull { state ->
          state.label == restoredSnapshot.avatarState
        } ?: AvatarState.Idle,
    )
  }
  var promptText by rememberSaveable {
    mutableStateOf(restoredSnapshot.promptText)
  }
  var sessionId by rememberSaveable { mutableStateOf(restoredSnapshot.sessionId) }
  var nativeReady by rememberSaveable { mutableStateOf(false) }
  var sessionReady by rememberSaveable { mutableStateOf(false) }
  var isForeground by rememberSaveable { mutableStateOf(true) }
  var serviceRunning by rememberSaveable { mutableStateOf(false) }
  var captureRequested by rememberSaveable { mutableStateOf(false) }
  var sensorsRunning by rememberSaveable { mutableStateOf(false) }
  var microphoneStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.microphoneStatus)
  }
  var cameraStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.cameraStatus)
  }
  var speakerStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.speakerStatus)
  }
  var speechLevel by rememberSaveable { mutableStateOf(0f) }
  var avatarMotionLevel by rememberSaveable { mutableStateOf(0f) }
  var lastAsrText by rememberSaveable { mutableStateOf(restoredSnapshot.lastAsrText) }
  var lastAsrStats by rememberSaveable { mutableStateOf(restoredSnapshot.lastAsrStats) }
  var lastVisionSummary by rememberSaveable {
    mutableStateOf(restoredSnapshot.lastVisionSummary)
  }
  var assistantStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.assistantStatus)
  }
  var assistantDraft by rememberSaveable { mutableStateOf("") }
  var lastAssistantReply by rememberSaveable {
    mutableStateOf(restoredSnapshot.lastAssistantReply)
  }
  var lastToolActivity by rememberSaveable {
    mutableStateOf(restoredSnapshot.lastToolActivity)
  }
  var runtimeProvider by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeProvider)
  }
  var runtimeDetail by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeDetail)
  }
  var runtimeModelsDir by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeModelsDir)
  }
  var hostWebSearchEnabled by rememberSaveable {
    mutableStateOf(restoredSnapshot.hostWebSearchEnabled)
  }
  var hostWebFetchEnabled by rememberSaveable {
    mutableStateOf(restoredSnapshot.hostWebFetchEnabled)
  }
  var runtimeDeviceBridgeStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeDeviceBridgeStatus)
  }
  var runtimeWebSearchStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeWebSearchStatus)
  }
  var runtimeWebFetchStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeWebFetchStatus)
  }
  var runtimeTextStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeTextStatus)
  }
  var runtimeVisionStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeVisionStatus)
  }
  var runtimeVulkanStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeVulkanStatus)
  }
  var runtimeLlmModel by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeLlmModel)
  }
  var runtimeVisionModel by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeVisionModel)
  }
  var runtimeMmprojModel by rememberSaveable {
    mutableStateOf(restoredSnapshot.runtimeMmprojModel)
  }
  var speechBackendStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechBackendStatus)
  }
  var speechAsrStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechAsrStatus)
  }
  var speechTtsStatus by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechTtsStatus)
  }
  var speechSttModel by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechSttModel)
  }
  var speechTtsVoice by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechTtsVoice)
  }
  var speechDetail by rememberSaveable {
    mutableStateOf(restoredSnapshot.speechDetail)
  }
  var permissionSummary by rememberSaveable {
    mutableStateOf(buildPermissionSummary(context))
  }
  var nativeDeviceStatus by rememberSaveable {
    mutableStateOf("No native device status published yet.")
  }
  var restoreRuntimePending by rememberSaveable {
    mutableStateOf(restoredSnapshot.shouldRestoreRuntime())
  }
  var restoreCapturePending by rememberSaveable {
    mutableStateOf(restoredSnapshot.restoreCapture)
  }
  var restoreFlowCompleted by rememberSaveable {
    mutableStateOf(!restoredSnapshot.shouldRestoreRuntime())
  }
  val permissionsGranted = hasAllRuntimePermissions(context)
  val currentSpeakerStatus = rememberUpdatedState(speakerStatus)
  val audioController =
      remember(bridge) {
        AudioCaptureController(
            bridge = bridge,
            onStateChanged = { state -> microphoneStatus = state },
            onSpeechLevelChanged = { level ->
              speechLevel = level
              if (currentSpeakerStatus.value != "speaking") {
                avatarMotionLevel = level
              }
            },
            onSpeechStarted = {
              if (currentSpeakerStatus.value == "speaking") {
                bridge.interruptGeneration()
                appendLog(logEntries, "host", "Barge-in: speech playback interrupted.")
              }
            },
        )
      }
  val cameraController =
      remember(context.applicationContext, bridge) {
        CameraFrameController(
            context = context.applicationContext,
            bridge = bridge,
            onStateChanged = { state -> cameraStatus = state },
        )
      }
  val speechController =
      remember(context.applicationContext) {
        SpeechPlaybackController(
            context = context.applicationContext,
            onStateChanged = { state -> speakerStatus = state },
            onSpeakingChanged = { speaking ->
              avatarState = if (speaking) AvatarState.Speak else AvatarState.Idle
              if (!speaking && microphoneStatus == "stopped") {
                avatarMotionLevel = 0f
              }
            },
            onEnvelopeChanged = { level ->
              avatarMotionLevel = level
            },
            onPlaybackEvent = { state ->
              bridge.reportTtsState(state)
            },
        )
      }
  val permissionLauncher =
      rememberLauncherForActivityResult(
          contract = ActivityResultContracts.RequestMultiplePermissions(),
      ) {
        permissionSummary = buildPermissionSummary(context)
        appendLog(
            logEntries,
            "host",
            "Permissions updated: $permissionSummary",
        )
      }

  fun startSensors() {
    if (!nativeReady || !sessionReady) {
      appendLog(logEntries, "host", "Start a session before enabling sensors.")
      return
    }
    if (!permissionsGranted) {
      appendLog(logEntries, "host", "Camera and microphone permissions are required.")
      return
    }

    captureRequested = true
    val audioStarted = audioController.start()
    val cameraStarted = cameraController.start(lifecycleOwner)
    sensorsRunning = audioStarted || cameraStarted
    appendLog(
        logEntries,
        "host",
        if (sensorsRunning) {
          "Live capture started."
        } else {
          "Live capture failed to start."
        },
    )
  }

  fun stopSensors(message: String = "Live capture stopped.") {
    restoreCapturePending = false
    captureRequested = false
    sensorsRunning = false
    audioController.stop()
    cameraController.stop()
    appendLog(logEntries, "host", message)
  }

  fun engineStateDirectory(): File {
    return File(context.filesDir, "ravbot-state").apply { mkdirs() }
  }

  fun engineModelsDirectory(): File {
    return File(context.filesDir, "ravbot-models").apply { mkdirs() }
  }

  fun initEngineFromHostStorage(): Boolean {
    return bridge.initEngine(
        configJson = buildSeedConfig(),
        stateDir = engineStateDirectory().absolutePath,
        modelsDir = engineModelsDirectory().absolutePath,
    )
  }

  DisposableEffect(bridge) {
    bridge.subscribeEvents { event ->
      appendLog(logEntries, event)
      parseAvatarState(event)?.let { state ->
        avatarState = state
      }
      parseJsonString(event.payload, "text")?.let { text ->
        when (event.name) {
          "mobile.asr_partial" -> {
            lastAsrText = "partial: $text"
            lastAsrStats = describeAsrStats(event.payload)
          }
          "mobile.asr_final" -> {
            lastAsrText = "final: $text"
            lastAsrStats = describeAsrStats(event.payload)
            assistantStatus = "queued_from_voice"
            assistantDraft = ""
          }
          "assistant_delta" -> {
            assistantDraft += text
            assistantStatus = "streaming"
          }
          "assistant_final" -> {
            lastAssistantReply = text
            assistantDraft = text
            assistantStatus =
                parseJsonString(event.payload, "finishReason")?.let { reason ->
                  "final:$reason"
                } ?: "final"
          }
          else -> Unit
        }
      }
      if (event.name == "tool_start") {
        val toolName = parseJsonString(event.payload, "name") ?: "unknown"
        lastToolActivity = "running: $toolName"
      }
      if (event.name == "tool_result") {
        val toolName = parseJsonString(event.payload, "name") ?: "unknown"
        val toolStatus = parseJsonString(event.payload, "status") ?: "unknown"
        lastToolActivity = "$toolName -> $toolStatus"
      }
      if (event.name == "mobile.vision_observation") {
        parseJsonString(event.payload, "summary")?.let { summary ->
          lastVisionSummary = summary
        }
      }
      if (event.name == "mobile.tts_state") {
        when (parseJsonString(event.payload, "state")) {
          "interrupted" -> {
            speechController.stop()
          }
          else -> Unit
        }
      }
      if (event.name == "device.speech_request") {
        parseJsonString(event.payload, "text")?.let { text ->
          speechController.speak(text)
        }
      }
      if (event.name == "device.speech_interrupt") {
        speechController.stop()
      }
      if (event.name == "device.avatar_state") {
        parseJsonString(event.payload, "state")?.let { stateName ->
          AvatarState.entries.firstOrNull { state -> state.label == stateName }?.let { state ->
            avatarState = state
          }
        }
      }
      if (event.name == "mobile.runtime_status") {
        runtimeProvider = parseJsonString(event.payload, "provider") ?: runtimeProvider
        runtimeDetail = parseJsonString(event.payload, "detail") ?: runtimeDetail
        runtimeModelsDir = parseJsonString(event.payload, "modelsDir") ?: runtimeModelsDir
        runtimeDeviceBridgeStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "deviceBridgeAttached"))
        runtimeWebSearchStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "webSearchReady"))
        runtimeWebFetchStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "webFetchReady"))
        runtimeTextStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "textReady"))
        runtimeVisionStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "visionReady"))
        runtimeVulkanStatus =
            describeVulkanStatus(
                parseJsonBoolean(event.payload, "vulkanRequested"),
                parseJsonBoolean(event.payload, "vulkanEnabled"),
            )
        runtimeLlmModel =
            describeModelArtifact(
                parseJsonString(event.payload, "llmModelPath"),
                parseJsonBoolean(event.payload, "llmModelExists"),
            )
        runtimeVisionModel =
            describeModelArtifact(
                parseJsonString(event.payload, "visionModelPath"),
                parseJsonBoolean(event.payload, "visionModelExists"),
            )
        runtimeMmprojModel =
            describeModelArtifact(
                parseJsonString(event.payload, "mmprojPath"),
                parseJsonBoolean(event.payload, "mmprojExists"),
            )
        speechBackendStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "speechBackendLinked"))
        speechAsrStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "asrReady"))
        speechTtsStatus =
            describeRuntimeFlag(parseJsonBoolean(event.payload, "ttsReady"))
        speechSttModel =
            describeModelArtifact(
                parseJsonString(event.payload, "sttModelPath"),
                parseJsonBoolean(event.payload, "sttModelExists"),
            )
        speechTtsVoice =
            describeModelArtifact(
                parseJsonString(event.payload, "ttsVoicePath"),
                parseJsonBoolean(event.payload, "ttsVoiceExists"),
            )
        speechDetail = parseJsonString(event.payload, "speechDetail") ?: speechDetail
      }
      if (event.name == "mobile.device_status") {
        nativeDeviceStatus = describeDeviceStatus(event.payload)
      }
    }

    onDispose {
      audioController.dispose()
      cameraController.dispose()
      speechController.dispose()
      bridge.dispose()
    }
  }

  LaunchedEffect(restoreRuntimePending) {
    if (!restoreRuntimePending) {
      return@LaunchedEffect
    }

    restoreRuntimePending = false
    if (!bridge.loadStatus.isLoaded) {
      appendLog(logEntries, "host", "Skipped snapshot restore: native library not loaded.")
      restoreCapturePending = false
      restoreFlowCompleted = true
      return@LaunchedEffect
    }

    if (restoredSnapshot.restoreEngine) {
      nativeReady = initEngineFromHostStorage()
      appendLog(
          logEntries,
          "host",
          if (nativeReady) {
            "Engine restored from host snapshot."
          } else {
            "Engine restore from host snapshot failed."
          },
      )
      if (nativeReady) {
        bridge.setForegroundState(isForeground)
      }
    }

    if (nativeReady && restoredSnapshot.restoreSession) {
      sessionReady = bridge.startSession(sessionId)
      appendLog(
          logEntries,
          "host",
          if (sessionReady) {
            "Session restored: $sessionId"
          } else {
            "Session restore failed: $sessionId"
          },
      )
    }

    if (restoredSnapshot.restoreService) {
      ContextCompat.startForegroundService(
          context,
          RavbotForegroundService.createStartIntent(
              context = context,
              sessionReady = sessionReady,
              captureRequested = captureRequested,
              microphoneStatus = microphoneStatus,
              cameraStatus = cameraStatus,
              speakerStatus = speakerStatus,
              assistantStatus = assistantStatus,
          ),
      )
      serviceRunning = true
      appendLog(logEntries, "host", "Foreground service restored from host snapshot.")
    }

    if (!restoredSnapshot.restoreCapture) {
      restoreCapturePending = false
    } else if (!permissionsGranted) {
      appendLog(
          logEntries,
          "host",
          "Capture restore is waiting for camera and microphone permissions.",
      )
    }

    restoreFlowCompleted = true
  }

  LaunchedEffect(
      restoreCapturePending,
      permissionsGranted,
      nativeReady,
      sessionReady,
      sensorsRunning,
  ) {
    if (!restoreCapturePending ||
        !permissionsGranted ||
        !nativeReady ||
        !sessionReady ||
        sensorsRunning) {
      return@LaunchedEffect
    }
    startSensors()
    restoreCapturePending = false
  }

  val hostSnapshot =
      HostSnapshot(
          sessionId = sessionId,
          promptText = promptText,
          avatarState = avatarState.label,
          restoreEngine = nativeReady,
          restoreSession = sessionReady,
          restoreService = serviceRunning,
          restoreCapture = captureRequested || restoreCapturePending,
          microphoneStatus = microphoneStatus,
          cameraStatus = cameraStatus,
          speakerStatus = speakerStatus,
          assistantStatus = assistantStatus,
          lastAsrText = lastAsrText,
          lastAsrStats = lastAsrStats,
          lastVisionSummary = lastVisionSummary,
          lastAssistantReply = lastAssistantReply,
          lastToolActivity = lastToolActivity,
          runtimeProvider = runtimeProvider,
          runtimeDetail = runtimeDetail,
          runtimeModelsDir = runtimeModelsDir,
          hostWebSearchEnabled = hostWebSearchEnabled,
          hostWebFetchEnabled = hostWebFetchEnabled,
          runtimeDeviceBridgeStatus = runtimeDeviceBridgeStatus,
          runtimeWebSearchStatus = runtimeWebSearchStatus,
          runtimeWebFetchStatus = runtimeWebFetchStatus,
          runtimeTextStatus = runtimeTextStatus,
          runtimeVisionStatus = runtimeVisionStatus,
          runtimeVulkanStatus = runtimeVulkanStatus,
          runtimeLlmModel = runtimeLlmModel,
          runtimeVisionModel = runtimeVisionModel,
          runtimeMmprojModel = runtimeMmprojModel,
          speechBackendStatus = speechBackendStatus,
          speechAsrStatus = speechAsrStatus,
          speechTtsStatus = speechTtsStatus,
          speechSttModel = speechSttModel,
          speechTtsVoice = speechTtsVoice,
          speechDetail = speechDetail,
      )

  LaunchedEffect(hostSnapshot, restoreFlowCompleted) {
    if (!restoreFlowCompleted) {
      return@LaunchedEffect
    }
    snapshotStore.save(hostSnapshot)
  }

  LaunchedEffect(
      nativeReady,
      serviceRunning,
      captureRequested,
      microphoneStatus,
      cameraStatus,
      speakerStatus,
      permissionsGranted,
  ) {
    if (!nativeReady) {
      return@LaunchedEffect
    }
    bridge.reportDeviceStatus(
        serviceRunning = serviceRunning,
        captureRequested = captureRequested,
        permissionsGranted = permissionsGranted,
        microphoneStatus = microphoneStatus,
        cameraStatus = cameraStatus,
        speakerStatus = speakerStatus,
    )
  }

  LaunchedEffect(nativeReady, hostWebSearchEnabled, hostWebFetchEnabled) {
    if (!nativeReady) {
      return@LaunchedEffect
    }
    bridge.setHostWebToolsEnabled(
        webSearchEnabled = hostWebSearchEnabled,
        webFetchEnabled = hostWebFetchEnabled,
    )
  }

  DisposableEffect(
      lifecycleOwner,
      nativeReady,
      sessionReady,
      captureRequested,
      permissionsGranted,
      bridge,
  ) {
    val observer =
        LifecycleEventObserver { _, event ->
          when (event) {
            Lifecycle.Event.ON_START -> {
              isForeground = true
              if (nativeReady) {
                bridge.setForegroundState(true)
              }
              if (captureRequested && permissionsGranted && sessionReady) {
                val audioStarted = audioController.start()
                val cameraStarted = cameraController.start(lifecycleOwner)
                sensorsRunning = audioStarted || cameraStarted
              }
            }
            Lifecycle.Event.ON_STOP -> {
              isForeground = false
              if (nativeReady) {
                bridge.setForegroundState(false)
              }
              if (sensorsRunning) {
                audioController.stop()
                cameraController.stop()
                sensorsRunning = false
                appendLog(
                    logEntries,
                    "host",
                    "Live capture paused while the host is backgrounded.",
                )
              }
            }
            else -> Unit
          }
        }
    lifecycleOwner.lifecycle.addObserver(observer)
    onDispose {
      lifecycleOwner.lifecycle.removeObserver(observer)
    }
  }

  Column(
      modifier =
          Modifier
              .fillMaxSize()
              .background(MaterialTheme.colorScheme.background)
              .verticalScroll(rememberScrollState())
              .padding(horizontal = 20.dp, vertical = 24.dp),
      verticalArrangement = Arrangement.spacedBy(16.dp),
  ) {
    Text(
        text = "RavBot Android Host",
        style = MaterialTheme.typography.headlineMedium,
        fontWeight = FontWeight.SemiBold,
    )
    Text(
        text =
            "Compose host for the embodied assistant MVP. AudioRecord and " +
                "CameraX now feed embedded ravbot_mobile_core over JNI, and " +
                "Android TTS can speak native replies while core-side " +
                "ASR/VLM/TTS providers remain placeholder implementations.",
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
    )

    Card(
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface,
            ),
    ) {
      Column(
          modifier = Modifier.padding(20.dp),
          verticalArrangement = Arrangement.spacedBy(16.dp),
      ) {
        AvatarFace(
            state = avatarState,
            motionLevel = avatarMotionLevel,
            modifier = Modifier.fillMaxWidth(),
        )

        Row(
            modifier =
                Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          AvatarState.entries.forEach { state ->
            AssistChip(
                onClick = { avatarState = state },
                label = { Text(state.label) },
            )
          }
        }
      }
    }

    StatusCard(
        title = "Native bridge",
        lines =
            listOf(
                "Library: ${bridge.loadStatus.detail}",
                "Engine handle: ${if (bridge.isInitialized()) "ready" else "not initialized"}",
                "Session: ${if (sessionReady) sessionId else "not started"}",
                "Foreground: ${if (isForeground) "true" else "false"}",
            ),
    )

    StatusCard(
        title = "On-device runtime",
        lines =
            listOf(
                "Provider: $runtimeProvider",
                "Host web search toggle: ${if (hostWebSearchEnabled) "enabled" else "disabled"}",
                "Host web fetch toggle: ${if (hostWebFetchEnabled) "enabled" else "disabled"}",
                "Device bridge: $runtimeDeviceBridgeStatus",
                "Web search: $runtimeWebSearchStatus",
                "Web fetch: $runtimeWebFetchStatus",
                "Text runtime: $runtimeTextStatus",
                "Vision runtime: $runtimeVisionStatus",
                "Vulkan: $runtimeVulkanStatus",
                "Speech backend: $speechBackendStatus",
                "ASR runtime: $speechAsrStatus",
                "TTS runtime: $speechTtsStatus",
                "Models dir: $runtimeModelsDir",
                "LLM: $runtimeLlmModel",
                "Vision model: $runtimeVisionModel",
                "MMProj: $runtimeMmprojModel",
                "STT asset: $speechSttModel",
                "TTS asset: $speechTtsVoice",
                "Detail: $runtimeDetail",
                "Speech detail: $speechDetail",
            ),
    )

    StatusCard(
        title = "Device readiness",
        lines =
            listOf(
                "Permissions: $permissionSummary",
                "Foreground service: ${if (serviceRunning) "running" else "stopped"}",
                "Capture requested: ${if (captureRequested) "true" else "false"}",
                "Native device status: $nativeDeviceStatus",
            ),
    )

    StatusCard(
        title = "Live capture",
        lines =
            listOf(
                "Sensors: ${if (sensorsRunning) "running" else "stopped"}",
                "Microphone: $microphoneStatus",
                "Camera: $cameraStatus",
                "Speaker: $speakerStatus",
                "Speech level: ${String.format(Locale.US, "%.02f", speechLevel)}",
                "Last ASR: $lastAsrText",
                "ASR stats: $lastAsrStats",
                "Last vision: $lastVisionSummary",
            ),
    )

    StatusCard(
        title = "Assistant turn",
        lines =
            listOf(
                "Status: $assistantStatus",
                "Streaming: ${assistantDraft.ifBlank { "No active assistant stream." }}",
                "Final reply: $lastAssistantReply",
                "Tool activity: $lastToolActivity",
            ),
    )

    Card(
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface,
            ),
    ) {
      Column(
          modifier = Modifier.padding(20.dp),
          verticalArrangement = Arrangement.spacedBy(12.dp),
      ) {
        OutlinedTextField(
            value = sessionId,
            onValueChange = { sessionId = it },
            modifier = Modifier.fillMaxWidth(),
            label = { Text("Session id") },
            singleLine = true,
        )
        OutlinedTextField(
            value = promptText,
            onValueChange = { promptText = it },
            modifier = Modifier.fillMaxWidth(),
            label = { Text("Prompt") },
            maxLines = 3,
        )

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          OutlinedButton(
              onClick = {
                hostWebSearchEnabled = !hostWebSearchEnabled
                appendLog(
                    logEntries,
                    "host",
                    "Host web search ${if (hostWebSearchEnabled) "enabled" else "disabled"}.",
                )
              },
          ) {
            Text(if (hostWebSearchEnabled) "Disable web search" else "Enable web search")
          }

          OutlinedButton(
              onClick = {
                hostWebFetchEnabled = !hostWebFetchEnabled
                appendLog(
                    logEntries,
                    "host",
                    "Host web fetch ${if (hostWebFetchEnabled) "enabled" else "disabled"}.",
                )
              },
          ) {
            Text(if (hostWebFetchEnabled) "Disable web fetch" else "Enable web fetch")
          }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          OutlinedButton(
              onClick = {
                permissionLauncher.launch(requiredRuntimePermissions())
              },
          ) {
            Text("Grant permissions")
          }

          OutlinedButton(
              onClick = {
                ContextCompat.startForegroundService(
                    context,
                    RavbotForegroundService.createStartIntent(
                        context = context,
                        sessionReady = sessionReady,
                        captureRequested = captureRequested,
                        microphoneStatus = microphoneStatus,
                        cameraStatus = cameraStatus,
                        speakerStatus = speakerStatus,
                        assistantStatus = assistantStatus,
                    ),
                )
                serviceRunning = true
                appendLog(logEntries, "host", "Foreground service start requested.")
              },
          ) {
            Text("Start runtime")
          }

          OutlinedButton(
              onClick = {
                context.startService(RavbotForegroundService.createStopIntent(context))
                serviceRunning = false
                appendLog(logEntries, "host", "Foreground service stop requested.")
              },
          ) {
            Text("Stop runtime")
          }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          Button(
              onClick = {
                stopSensors("Live capture stopped before engine re-init.")
                sessionReady = false
                restoreCapturePending = false
                nativeReady = initEngineFromHostStorage()
                appendLog(
                    logEntries,
                    "host",
                    if (nativeReady) "Engine initialized." else "Engine init failed.",
                )
                if (nativeReady) {
                  bridge.setForegroundState(isForeground)
                }
              },
              enabled = bridge.loadStatus.isLoaded,
          ) {
            Text("Init engine")
          }

          OutlinedButton(
              onClick = {
                val started = bridge.startSession(sessionId)
                sessionReady = started
                appendLog(
                    logEntries,
                    "host",
                    if (started) "Session started: $sessionId" else "Session start failed.",
                )
              },
              enabled = nativeReady,
          ) {
            Text("Start session")
          }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          Button(
              onClick = {
                assistantStatus = "queued"
                assistantDraft = ""
                val sent = bridge.sendTextTurn(promptText)
                appendLog(
                    logEntries,
                    "host",
                    if (sent) "Text turn queued." else "Text turn rejected.",
                )
              },
              enabled = nativeReady,
          ) {
            Text("Send text")
          }

          OutlinedButton(
              onClick = {
                bridge.interruptGeneration()
                speechController.stop()
                appendLog(logEntries, "host", "Interrupt requested.")
              },
              enabled = nativeReady,
          ) {
            Text("Interrupt")
          }
        }

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
          Button(
              onClick = {
                startSensors()
              },
              enabled = nativeReady && sessionReady && permissionsGranted,
          ) {
            Text("Start sensors")
          }

          OutlinedButton(
              onClick = {
                stopSensors()
              },
              enabled = captureRequested || sensorsRunning,
          ) {
            Text("Stop sensors")
          }
        }
      }
    }

    StatusCard(
        title = "Current host wiring",
        lines =
            listOf(
                "Speech: AudioRecord + JNI -> SpeechPipeline facade with sherpa-onnx diagnostics",
                "Vision: CameraX ImageAnalysis -> JNI -> placeholder vision provider",
                "Voice out: mobile.tts_state -> Android TextToSpeech playback",
                "Brain: ravbot_mobile_core with native model resolution and llama.cpp diagnostics",
                "Memory: local state dir for sessions and structured recalls",
            ),
    )

    Card(
        colors =
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surface,
            ),
    ) {
      Column(
          modifier = Modifier.padding(20.dp),
          verticalArrangement = Arrangement.spacedBy(8.dp),
      ) {
        Text(
            text = "Event log",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Medium,
        )
        if (logEntries.isEmpty()) {
          Text(
              text = "No events yet.",
              color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
          )
        } else {
          logEntries.forEach { entry ->
            Text(
                text = entry,
                style = MaterialTheme.typography.bodySmall,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
          }
        }
      }
    }
  }

  LaunchedEffect(
      serviceRunning,
      sessionReady,
      captureRequested,
      microphoneStatus,
      cameraStatus,
      speakerStatus,
      assistantStatus,
  ) {
    if (!serviceRunning) {
      return@LaunchedEffect
    }
    context.startService(
        RavbotForegroundService.createStatusIntent(
            context = context,
            sessionReady = sessionReady,
            captureRequested = captureRequested,
            microphoneStatus = microphoneStatus,
            cameraStatus = cameraStatus,
            speakerStatus = speakerStatus,
            assistantStatus = assistantStatus,
        ),
    )
  }
}

@Composable
private fun AvatarFace(
    state: AvatarState,
    motionLevel: Float,
    modifier: Modifier = Modifier,
) {
  var blink by remember { mutableStateOf(false) }
  val outline = MaterialTheme.colorScheme.primary
  val faceColor =
      when (state) {
        AvatarState.Error -> Color(0xFFF9C4C0)
        AvatarState.Watch -> Color(0xFFE4F6F8)
        else -> Color(0xFFFFF5DA)
      }

  LaunchedEffect(state) {
    while (true) {
      delay(if (state == AvatarState.Listen) 1800L else 3200L)
      blink = true
      delay(150L)
      blink = false
    }
  }

  val mouthOpen by animateFloatAsState(
      targetValue =
          when (state) {
            AvatarState.Idle -> 0.12f
            AvatarState.Listen -> 0.10f + motionLevel.coerceIn(0f, 1f) * 0.28f
            AvatarState.Think -> 0.08f
            AvatarState.Speak -> 0.16f + motionLevel.coerceIn(0f, 1f) * 0.48f
            AvatarState.Watch -> 0.28f
            AvatarState.Error -> 0.18f
          },
      label = "mouth_open",
  )

  Box(
      modifier = modifier,
      contentAlignment = Alignment.Center,
  ) {
    Canvas(
        modifier =
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .padding(8.dp),
    ) {
      val center = center
      val radius = size.minDimension * 0.42f

      drawCircle(
          color = faceColor,
          radius = radius,
          center = center,
      )
      drawCircle(
          color = outline,
          radius = radius,
          center = center,
          style = Stroke(width = size.minDimension * 0.02f),
      )

      val eyeY = center.y - radius * 0.18f
      val leftEyeX = center.x - radius * 0.28f
      val rightEyeX = center.x + radius * 0.28f
      val eyeWidth = radius * 0.16f

      if (blink || state == AvatarState.Think) {
        drawLine(
            color = outline,
            start = Offset(leftEyeX - eyeWidth, eyeY),
            end = Offset(leftEyeX + eyeWidth, eyeY),
            strokeWidth = radius * 0.05f,
            cap = StrokeCap.Round,
        )
        drawLine(
            color = outline,
            start = Offset(rightEyeX - eyeWidth, eyeY),
            end = Offset(rightEyeX + eyeWidth, eyeY),
            strokeWidth = radius * 0.05f,
            cap = StrokeCap.Round,
        )
      } else {
        drawCircle(color = outline, radius = radius * 0.07f, center = Offset(leftEyeX, eyeY))
        drawCircle(color = outline, radius = radius * 0.07f, center = Offset(rightEyeX, eyeY))
      }

      when (state) {
        AvatarState.Listen -> {
          drawCircle(
              color = Color(0x220A9396),
              radius = radius * (1.03f + motionLevel.coerceIn(0f, 1f) * 0.08f),
              center = center,
              style =
                  Stroke(
                      width =
                          size.minDimension *
                              (0.014f + motionLevel.coerceIn(0f, 1f) * 0.01f),
                  ),
          )
        }
        AvatarState.Think -> {
          drawArc(
              color = outline,
              startAngle = 200f,
              sweepAngle = 140f,
              useCenter = false,
              topLeft = Offset(center.x - radius * 0.28f, center.y - radius * 0.7f),
              size = Size(radius * 0.56f, radius * 0.28f),
              style = Stroke(width = radius * 0.05f, cap = StrokeCap.Round),
          )
        }
        AvatarState.Watch -> {
          drawCircle(
              color = Color(0x22CA6702),
              radius = radius * 0.2f,
              center = Offset(center.x, center.y - radius * 0.5f),
          )
        }
        else -> Unit
      }

      val mouthTop = center.y + radius * 0.18f
      val mouthWidth = radius * 0.42f
      when (state) {
        AvatarState.Error -> {
          val path =
              Path().apply {
                moveTo(center.x - mouthWidth, mouthTop + radius * 0.18f)
                quadraticTo(
                    center.x,
                    mouthTop - radius * 0.08f,
                    center.x + mouthWidth,
                    mouthTop + radius * 0.18f,
                )
              }
          drawPath(
              path = path,
              color = outline,
              style = Stroke(width = radius * 0.05f, cap = StrokeCap.Round),
          )
        }
        AvatarState.Think -> {
          drawLine(
              color = outline,
              start = Offset(center.x - mouthWidth * 0.7f, mouthTop + radius * 0.08f),
              end = Offset(center.x + mouthWidth * 0.7f, mouthTop + radius * 0.08f),
              strokeWidth = radius * 0.05f,
              cap = StrokeCap.Round,
          )
        }
        else -> {
          val mouthHeight = radius * mouthOpen
          drawRoundRect(
              color = outline,
              topLeft = Offset(center.x - mouthWidth, mouthTop),
              size = Size(mouthWidth * 2f, mouthHeight * 1.6f),
              cornerRadius = CornerRadius(mouthHeight, mouthHeight),
              style = Stroke(width = radius * 0.05f),
          )
        }
      }
    }
  }
}

@Composable
private fun StatusCard(
    title: String,
    lines: List<String>,
) {
  Card(
      colors =
          CardDefaults.cardColors(
              containerColor = MaterialTheme.colorScheme.surface,
          ),
  ) {
    Column(
        modifier = Modifier.padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
      Text(
          text = title,
          style = MaterialTheme.typography.titleMedium,
          fontWeight = FontWeight.Medium,
      )
      lines.forEach { line ->
        Text(
            text = line,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.8f),
        )
      }
    }
  }
}

private fun buildSeedConfig(): String {
  return """
    {
      "mobile": {
        "runtime": {
          "enabled": true,
          "mode": "local",
          "foregroundOnly": true,
          "continuousVision": true
        },
        "models": {
          "llmModel": "Qwen3.5-0.8B-Q4_K_M.gguf",
          "vlmModel": "SmolVLM-500M-Instruct-Q8_0.gguf",
          "mmprojModel": "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf",
          "sttModel": "SenseVoiceSmall",
          "ttsVoice": "kokoro-multi-lang-v1_1",
          "useVulkan": true
        },
        "audio": {
          "autoListen": true,
          "tapToTalkEnabled": true,
          "wakeWordEnabled": false,
          "bargeInEnabled": true,
          "sampleRate": 16000
        },
        "vision": {
          "enabled": true,
          "sampleFps": 0.5,
          "foregroundOnly": true,
          "persistRawFrames": false,
          "sceneChangeThreshold": 0.2
        },
        "avatar": {
          "renderer": "face2d",
          "defaultState": "idle",
          "lipSyncEnabled": true
        }
      }
    }
  """.trimIndent()
}

private fun requiredRuntimePermissions(): Array<String> {
  val permissions =
      mutableListOf(
          Manifest.permission.CAMERA,
          Manifest.permission.RECORD_AUDIO,
      )
  if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
    permissions += Manifest.permission.POST_NOTIFICATIONS
  }
  return permissions.toTypedArray()
}

private fun buildPermissionSummary(context: Context): String {
  val required = requiredRuntimePermissions()
  val missing =
      required.filter { permission ->
        ContextCompat.checkSelfPermission(context, permission) !=
            PackageManager.PERMISSION_GRANTED
      }
  if (missing.isEmpty()) {
    return "${required.size}/${required.size} granted"
  }

  return "${required.size - missing.size}/${required.size} granted, missing: " +
      missing.joinToString(", ") { permissionLabel(it) }
}

private fun hasAllRuntimePermissions(context: Context): Boolean {
  return requiredRuntimePermissions().all { permission ->
    ContextCompat.checkSelfPermission(context, permission) ==
        PackageManager.PERMISSION_GRANTED
  }
}

private fun permissionLabel(permission: String): String {
  return when (permission) {
    Manifest.permission.CAMERA -> "camera"
    Manifest.permission.RECORD_AUDIO -> "microphone"
    Manifest.permission.POST_NOTIFICATIONS -> "notifications"
    else -> permission
  }
}

private fun parseAvatarState(event: NativeEvent): AvatarState? {
  if (event.name != "mobile.avatar_state") {
    return null
  }

  val stateName = parseJsonString(event.payload, "state")
  return AvatarState.entries.firstOrNull { state ->
    state.label == stateName
  }
}

private fun parseJsonString(payload: String, field: String): String? {
  return runCatching {
        JSONObject(payload).optString(field).takeIf { value -> value.isNotBlank() }
      }
      .getOrNull()
}

private fun parseJsonBoolean(payload: String, field: String): Boolean? {
  return runCatching {
        val json = JSONObject(payload)
        if (!json.has(field)) {
          null
        } else {
          json.optBoolean(field)
        }
      }
      .getOrNull()
}

private fun parseJsonInt(payload: String, field: String): Int? {
  return runCatching {
        val json = JSONObject(payload)
        if (!json.has(field)) {
          null
        } else {
          json.optInt(field)
        }
      }
      .getOrNull()
}

private fun parseJsonDouble(payload: String, field: String): Double? {
  return runCatching {
        val json = JSONObject(payload)
        if (!json.has(field)) {
          null
        } else {
          json.optDouble(field)
        }
      }
      .getOrNull()
}

private fun describeAsrStats(payload: String): String {
  val segmentIndex = parseJsonInt(payload, "segmentIndex")
  val durationMs = parseJsonInt(payload, "durationMs")
  val sampleRateHz = parseJsonInt(payload, "sampleRateHz")
  val averageLevel = parseJsonDouble(payload, "averageLevel")
  val peakLevel = parseJsonDouble(payload, "peakLevel")
  val endReason = parseJsonString(payload, "endReason")

  val stats = mutableListOf<String>()
  segmentIndex?.let { stats += "segment $it" }
  durationMs?.let { stats += "${it}ms" }
  sampleRateHz?.let { stats += "${it}Hz" }
  averageLevel?.let { level ->
    stats += "avg ${String.format(Locale.US, "%.02f", level)}"
  }
  peakLevel?.let { level ->
    stats += "peak ${String.format(Locale.US, "%.02f", level)}"
  }
  endReason?.let { stats += it }

  return if (stats.isEmpty()) {
    "No ASR metadata yet."
  } else {
    stats.joinToString(" | ")
  }
}

private fun describeDeviceStatus(payload: String): String {
  val foreground = parseJsonBoolean(payload, "foreground")
  val serviceRunning = parseJsonBoolean(payload, "serviceRunning")
  val captureRequested = parseJsonBoolean(payload, "captureRequested")
  val permissionsGranted = parseJsonBoolean(payload, "permissionsGranted")
  val microphoneStatus = parseJsonString(payload, "microphoneStatus")
  val cameraStatus = parseJsonString(payload, "cameraStatus")
  val speakerStatus = parseJsonString(payload, "speakerStatus")

  val parts = mutableListOf<String>()
  foreground?.let { parts += if (it) "foreground" else "background" }
  serviceRunning?.let { parts += if (it) "service on" else "service off" }
  captureRequested?.let { parts += if (it) "capture on" else "capture off" }
  permissionsGranted?.let { parts += if (it) "permissions ok" else "permissions missing" }
  microphoneStatus?.let { parts += "mic $it" }
  cameraStatus?.let { parts += "cam $it" }
  speakerStatus?.let { parts += "speaker $it" }

  return if (parts.isEmpty()) {
    "No native device status published yet."
  } else {
    parts.joinToString(" | ")
  }
}

private fun describeRuntimeFlag(value: Boolean?): String {
  return when (value) {
    true -> "ready"
    false -> "not ready"
    null -> "unknown"
  }
}

private fun describeVulkanStatus(
    requested: Boolean?,
    enabled: Boolean?,
): String {
  return when {
    requested == null && enabled == null -> "unknown"
    requested == true && enabled == true -> "requested + enabled"
    requested == true && enabled == false -> "requested, fell back"
    requested == false && enabled == false -> "disabled"
    else -> "unknown"
  }
}

private fun describeModelArtifact(
    path: String?,
    exists: Boolean?,
): String {
  if (path.isNullOrBlank()) {
    return "not configured"
  }
  val prefix =
      when (exists) {
        true -> "present"
        false -> "missing"
        null -> "unknown"
      }
  return "$prefix: $path"
}

private fun appendLog(
    logEntries: MutableList<String>,
    event: NativeEvent,
) {
  appendLog(
      logEntries = logEntries,
      tag = event.name,
      message = event.payload,
      timestampMillis = event.timestampMillis,
  )
}

private fun appendLog(
    logEntries: MutableList<String>,
    tag: String,
    message: String,
    timestampMillis: Long = System.currentTimeMillis(),
) {
  val timestamp =
      SimpleDateFormat("HH:mm:ss", Locale.US).format(Date(timestampMillis))
  logEntries.add(0, "[$timestamp] $tag: $message")
  while (logEntries.size > 10) {
    logEntries.removeLast()
  }
}
