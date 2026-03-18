package com.ravbot.android

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
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
  }
}
