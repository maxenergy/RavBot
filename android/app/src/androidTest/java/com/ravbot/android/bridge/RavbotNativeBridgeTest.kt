package com.ravbot.android.bridge

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class RavbotNativeBridgeTest {
  @Test
  fun reportDeviceStatusPublishesHostCapabilityFlags() {
    val context = ApplicationProvider.getApplicationContext<Context>()
    val stateDir = testDir(context, "bridge-state")
    val modelsDir = testDir(context, "bridge-models")
    val bridge = RavbotNativeBridge()
    val eventRef = AtomicReference<NativeEvent?>()
    val latch = CountDownLatch(1)

    bridge.subscribeEvents { event ->
      if (event.name == "mobile.device_status") {
        eventRef.set(event)
        latch.countDown()
      }
    }

    try {
      assertTrue(
          bridge.initEngine(
              configJson = seedConfigJson(),
              stateDir = stateDir.absolutePath,
              modelsDir = modelsDir.absolutePath,
          ),
      )

      bridge.reportDeviceStatus(
          serviceRunning = true,
          captureRequested = true,
          permissionsGranted = true,
          hostWebSearchEnabled = false,
          hostWebFetchEnabled = true,
          hostHapticsEnabled = false,
          microphoneStatus = "running",
          cameraStatus = "running",
          speakerStatus = "speaking",
      )

      assertTrue(latch.await(5, TimeUnit.SECONDS))
      val event = eventRef.get()
      assertNotNull(event)
      val payload = requireNotNull(event).payload
      assertTrue(payload.contains("\"sessionKey\":\"android-mvp\""))
      assertTrue(payload.contains("\"hostWebSearchEnabled\":false"))
      assertTrue(payload.contains("\"hostWebFetchEnabled\":true"))
      assertTrue(payload.contains("\"hostHapticsEnabled\":false"))
      assertTrue(payload.contains("\"speakerStatus\":\"speaking\""))
    } finally {
      bridge.dispose()
      stateDir.deleteRecursively()
      modelsDir.deleteRecursively()
    }
  }

  private fun testDir(context: Context, name: String): File {
    val dir = File(context.cacheDir, "ravbot-native-bridge-test/$name")
    dir.deleteRecursively()
    dir.mkdirs()
    return dir
  }

  private fun seedConfigJson(): String =
      """
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
