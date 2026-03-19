package com.ravbot.android.ui

import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class HostSnapshotStoreTest {
  @Test
  fun saveLoadAndClearSnapshot() {
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    val store = HostSnapshotStore(context)
    store.clear()

    val snapshot =
        HostSnapshot(
            sessionId = "restore-me",
            promptText = "Describe the room.",
            avatarState = "watch",
            restoreEngine = true,
            restoreSession = true,
            restoreService = true,
            restoreCapture = true,
            microphoneStatus = "running",
            cameraStatus = "running",
            speakerStatus = "speaking",
            assistantStatus = "streaming",
            lastAsrText = "final: hello",
            lastAsrStats = "segment 2 | 320ms",
            lastVisionSummary = "desk and monitor",
            lastAssistantReply = "I can see a monitor.",
            lastToolActivity = "memory.search -> ok",
            runtimeProvider = "llama.cpp",
            runtimeDetail = "backend linked",
            runtimeModelsDir = "/data/user/0/com.ravbot.android/files/ravbot-models",
            runtimeVisionProvider = "placeholder_mobile_vision",
            runtimeVisionDetail = "placeholder backend active",
            runtimeAvailableTools = "device_status, runtime_status, camera_snapshot",
            runtimeToolAvailability =
                "web_fetch (device_bridge_missing), vibrate (vibration_unsupported)",
            hostWebSearchEnabled = false,
            hostWebFetchEnabled = true,
            hostHapticsEnabled = false,
            runtimeDeviceBridgeStatus = "ready",
            runtimeWebSearchStatus = "ready",
            runtimeWebFetchStatus = "disabled",
            runtimeHapticsStatus = "ready",
            runtimeTextStatus = "ready",
            runtimeVisionStatus = "ready",
            runtimeVulkanStatus = "requested + enabled",
            runtimeLlmModel = "present: /models/qwen.gguf",
            runtimeVisionModel = "present: /models/smolvlm.gguf",
            runtimeMmprojModel = "present: /models/mmproj.gguf",
            speechBackendStatus = "ready",
            speechAsrStatus = "ready",
            speechTtsStatus = "ready",
            speechSttModel = "present: /models/sensevoice",
            speechTtsVoice = "present: /models/kokoro",
            speechDetail = "speech backend linked",
            speechStateStatus = "speech capturing seg 2 320ms",
        )

    store.save(snapshot)
    val restored = store.load()

    assertEquals(snapshot, restored)
    assertTrue(restored.shouldRestoreRuntime())

    store.clear()
    val cleared = store.load()

    assertEquals("android-mvp", cleared.sessionId)
    assertEquals("What can you see and hear right now?", cleared.promptText)
    assertEquals("unknown", cleared.runtimeVisionProvider)
    assertEquals("No native vision diagnostics emitted yet.", cleared.runtimeVisionDetail)
    assertEquals("No mobile-safe tools advertised yet.", cleared.runtimeAvailableTools)
    assertEquals(
        "No per-tool availability diagnostics emitted yet.",
        cleared.runtimeToolAvailability,
    )
    assertTrue(cleared.hostWebSearchEnabled)
    assertTrue(cleared.hostWebFetchEnabled)
    assertTrue(cleared.hostHapticsEnabled)
    assertEquals("unknown", cleared.runtimeDeviceBridgeStatus)
    assertEquals("unknown", cleared.runtimeWebSearchStatus)
    assertEquals("unknown", cleared.runtimeWebFetchStatus)
    assertEquals("unknown", cleared.runtimeHapticsStatus)
    assertFalse(cleared.shouldRestoreRuntime())
  }
}
