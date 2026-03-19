package com.ravbot.android.bridge

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.assertEquals
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

  @Test
  fun resubscribeAfterUnsubscribePublishesFreshEvents() {
    val context = ApplicationProvider.getApplicationContext<Context>()
    val stateDir = testDir(context, "bridge-resubscribe-state")
    val modelsDir = testDir(context, "bridge-resubscribe-models")
    val bridge = RavbotNativeBridge()
    val firstEventRef = AtomicReference<NativeEvent?>()
    val secondEventRef = AtomicReference<NativeEvent?>()
    val firstLatch = CountDownLatch(1)
    val secondLatch = CountDownLatch(1)

    try {
      assertTrue(
          bridge.initEngine(
              configJson = seedConfigJson(),
              stateDir = stateDir.absolutePath,
              modelsDir = modelsDir.absolutePath,
          ),
      )

      bridge.subscribeEvents { event ->
        if (event.name == "mobile.device_status") {
          firstEventRef.set(event)
          firstLatch.countDown()
        }
      }
      bridge.reportDeviceStatus(
          serviceRunning = true,
          captureRequested = false,
          permissionsGranted = true,
          hostWebSearchEnabled = true,
          hostWebFetchEnabled = true,
          hostHapticsEnabled = false,
          microphoneStatus = "running",
          cameraStatus = "stopped",
          speakerStatus = "idle",
      )

      assertTrue(firstLatch.await(5, TimeUnit.SECONDS))
      bridge.unsubscribeEvents()

      bridge.subscribeEvents { event ->
        if (event.name == "mobile.device_status") {
          secondEventRef.set(event)
          secondLatch.countDown()
        }
      }
      bridge.reportDeviceStatus(
          serviceRunning = false,
          captureRequested = true,
          permissionsGranted = true,
          hostWebSearchEnabled = false,
          hostWebFetchEnabled = true,
          hostHapticsEnabled = false,
          microphoneStatus = "stopped",
          cameraStatus = "running",
          speakerStatus = "speaking",
      )

      assertTrue(secondLatch.await(5, TimeUnit.SECONDS))
      assertNotNull(firstEventRef.get())
      assertNotNull(secondEventRef.get())
      assertTrue(requireNotNull(firstEventRef.get()).payload.contains("\"serviceRunning\":true"))
      assertTrue(requireNotNull(secondEventRef.get()).payload.contains("\"serviceRunning\":false"))
      assertTrue(requireNotNull(secondEventRef.get()).payload.contains("\"cameraStatus\":\"running\""))
      assertEquals(false, requireNotNull(secondEventRef.get()).payload.contains("\"serviceRunning\":true"))
    } finally {
      bridge.dispose()
      stateDir.deleteRecursively()
      modelsDir.deleteRecursively()
    }
  }

  @Test
  fun captureControlHandlerRunsOnNativeRequest() {
    val bridge = RavbotNativeBridge()
    val resultRef = AtomicReference<String>()

    try {
      bridge.setCaptureControlHandler { sessionId, enabled ->
        resultRef.set("$sessionId:$enabled")
        """{"accepted":true,"requested":$enabled,"detail":"ok"}"""
      }

      val response = bridge.onNativeCaptureControl("agent:main:android", true)
      assertEquals("agent:main:android:true", resultRef.get())
      assertTrue(response.contains("\"accepted\":true"))
      assertTrue(response.contains("\"requested\":true"))
    } finally {
      bridge.dispose()
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
