package com.ravbot.android

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.core.app.ApplicationProvider
import com.ravbot.android.service.RavbotForegroundService
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class MainActivityTest {
  @get:Rule
  val composeRule = createAndroidComposeRule<MainActivity>()

  @Test
  fun hostScreenRendersTitleAndBridgeCard() {
    composeRule.onNodeWithText("RavBot Android Host").assertIsDisplayed()
    composeRule.onNodeWithText("Native bridge").assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsCaptureControls() {
    composeRule.onNodeWithText("Live capture").assertIsDisplayed()
    composeRule.onNodeWithText("Start sensors").assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsAndTogglesHostCapabilityControls() {
    composeRule.onNodeWithText("Disable web search").assertIsDisplayed()
    composeRule.onNodeWithText("Disable web fetch").assertIsDisplayed()
    composeRule.onNodeWithText("Disable haptics").assertIsDisplayed()
    composeRule.onNodeWithText("Host web search toggle: enabled").assertIsDisplayed()
    composeRule.onNodeWithText("Host web fetch toggle: enabled").assertIsDisplayed()
    composeRule.onNodeWithText("Host haptics toggle: enabled").assertIsDisplayed()

    composeRule.onNodeWithText("Disable web search").performClick()
    composeRule.onNodeWithText("Disable web fetch").performClick()
    composeRule.onNodeWithText("Disable haptics").performClick()

    composeRule.onNodeWithText("Enable web search").assertIsDisplayed()
    composeRule.onNodeWithText("Enable web fetch").assertIsDisplayed()
    composeRule.onNodeWithText("Enable haptics").assertIsDisplayed()
    composeRule.onNodeWithText("Host web search toggle: disabled")
        .assertIsDisplayed()
    composeRule.onNodeWithText("Host web fetch toggle: disabled")
        .assertIsDisplayed()
    composeRule.onNodeWithText("Host haptics toggle: disabled")
        .assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsHapticsRuntimeLine() {
    composeRule.onNodeWithText("Haptics: unknown").assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsSpeechToolAndCaptureControlRuntimeLines() {
    composeRule.onNodeWithText("Speech tool: unknown").assertIsDisplayed()
    composeRule.onNodeWithText("Capture control: unknown").assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsVisionProviderRuntimeLine() {
    composeRule.onNodeWithText("Vision provider: unknown").assertIsDisplayed()
  }

  @Test
  fun hostScreenShowsSessionBindingLine() {
    composeRule.onNodeWithText("Session binding: not started").assertIsDisplayed()
  }

  @Test
  fun foregroundServiceStatusIntentCarriesRuntimeSnapshot() {
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    val intent =
        RavbotForegroundService.createStatusIntent(
            context = context,
            sessionReady = true,
            captureRequested = true,
            microphoneStatus = "running",
            cameraStatus = "running",
            speakerStatus = "speaking",
            assistantStatus = "streaming",
            hostWebSearchEnabled = false,
            hostWebFetchEnabled = true,
            hostHapticsEnabled = false,
            runtimeHapticsStatus = "ready",
            runtimeSpeechToolStatus = "ready",
            runtimeCaptureControlStatus = "host_toggle_off",
            speechStateStatus = "speech capturing seg 2 320ms",
        )

    assertEquals(RavbotForegroundService.ACTION_UPDATE_STATUS, intent.action)
    assertTrue(intent.getBooleanExtra(RavbotForegroundService.EXTRA_SESSION_READY, false))
    assertTrue(intent.getBooleanExtra(RavbotForegroundService.EXTRA_CAPTURE_REQUESTED, false))
    assertEquals(
        "running",
        intent.getStringExtra(RavbotForegroundService.EXTRA_MICROPHONE_STATUS),
    )
    assertEquals(
        "running",
        intent.getStringExtra(RavbotForegroundService.EXTRA_CAMERA_STATUS),
    )
    assertEquals(
        "speaking",
        intent.getStringExtra(RavbotForegroundService.EXTRA_SPEAKER_STATUS),
    )
    assertEquals(
        "streaming",
        intent.getStringExtra(RavbotForegroundService.EXTRA_ASSISTANT_STATUS),
    )
    assertEquals(
        false,
        intent.getBooleanExtra(RavbotForegroundService.EXTRA_HOST_WEB_SEARCH_ENABLED, true),
    )
    assertEquals(
        true,
        intent.getBooleanExtra(RavbotForegroundService.EXTRA_HOST_WEB_FETCH_ENABLED, false),
    )
    assertEquals(
        false,
        intent.getBooleanExtra(RavbotForegroundService.EXTRA_HOST_HAPTICS_ENABLED, true),
    )
    assertEquals(
        "ready",
        intent.getStringExtra(RavbotForegroundService.EXTRA_RUNTIME_HAPTICS_STATUS),
    )
    assertEquals(
        "ready",
        intent.getStringExtra(RavbotForegroundService.EXTRA_RUNTIME_SPEECH_TOOL_STATUS),
    )
    assertEquals(
        "host_toggle_off",
        intent.getStringExtra(RavbotForegroundService.EXTRA_RUNTIME_CAPTURE_CONTROL_STATUS),
    )
    assertEquals(
        "speech capturing seg 2 320ms",
        intent.getStringExtra(RavbotForegroundService.EXTRA_SPEECH_STATE_STATUS),
    )
  }

  @Test
  fun foregroundServiceStateBroadcastUpdatesHostStatusLine() {
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    context.sendBroadcast(
        RavbotForegroundService.createServiceStateChangedIntent(
            context = context,
            running = true,
        ),
    )

    composeRule.waitUntil(timeoutMillis = 5_000) {
      composeRule.onAllNodesWithText("Foreground service: running")
          .fetchSemanticsNodes().isNotEmpty()
    }
    composeRule.onNodeWithText("Foreground service: running").assertIsDisplayed()

    context.sendBroadcast(
        RavbotForegroundService.createServiceStateChangedIntent(
            context = context,
            running = false,
        ),
    )

    composeRule.waitUntil(timeoutMillis = 5_000) {
      composeRule.onAllNodesWithText("Foreground service: stopped")
          .fetchSemanticsNodes().isNotEmpty()
    }
    composeRule.onNodeWithText("Foreground service: stopped").assertIsDisplayed()
  }

  @Test
  fun foregroundServicePersistsRunningState() {
    val context = ApplicationProvider.getApplicationContext<android.content.Context>()

    RavbotForegroundService.persistRunningState(context, true)
    assertTrue(RavbotForegroundService.loadPersistedRunningState(context))

    RavbotForegroundService.persistRunningState(context, false)
    assertEquals(false, RavbotForegroundService.loadPersistedRunningState(context))
  }
}
