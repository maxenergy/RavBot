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
            hostWebSearchEnabled = false,
            hostWebFetchEnabled = true,
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
        )

    store.save(snapshot)
    val restored = store.load()

    assertEquals(snapshot, restored)
    assertTrue(restored.shouldRestoreRuntime())

    store.clear()
    val cleared = store.load()

    assertEquals("android-mvp", cleared.sessionId)
    assertEquals("What can you see and hear right now?", cleared.promptText)
    assertTrue(cleared.hostWebSearchEnabled)
    assertTrue(cleared.hostWebFetchEnabled)
    assertEquals("unknown", cleared.runtimeDeviceBridgeStatus)
    assertEquals("unknown", cleared.runtimeWebSearchStatus)
    assertEquals("unknown", cleared.runtimeWebFetchStatus)
    assertEquals("unknown", cleared.runtimeHapticsStatus)
    assertFalse(cleared.shouldRestoreRuntime())
  }
}
