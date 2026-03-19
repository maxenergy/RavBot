package com.ravbot.android.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class RuntimeStatusParsingTest {
  @Test
  fun effectiveHostHapticsEnabledRequiresDeviceAvailability() {
    assertEquals(true, effectiveHostHapticsEnabled(true, true))
    assertEquals(false, effectiveHostHapticsEnabled(true, false))
    assertEquals(false, effectiveHostHapticsEnabled(false, true))
  }

  @Test
  fun parseVisionRuntimeReadyPrefersProviderScopedFlag() {
    val payload =
        """
          {
            "visionProviderReady": false,
            "visionReady": true
          }
        """.trimIndent()

    assertEquals(false, parseVisionRuntimeReady(payload))
  }

  @Test
  fun parseVisionRuntimeReadyFallsBackToLegacyFlag() {
    val payload =
        """
          {
            "visionReady": true
          }
        """.trimIndent()

    assertEquals(true, parseVisionRuntimeReady(payload))
  }

  @Test
  fun describeAvailableToolsJoinsAdvertisedToolNames() {
    val payload =
        """
          {
            "availableTools": [
              "device_status",
              "runtime_status",
              "camera_snapshot"
            ]
          }
        """.trimIndent()

    assertEquals(
        "device_status, runtime_status, camera_snapshot",
        describeAvailableTools(payload),
    )
  }

  @Test
  fun describeToolAvailabilityListsBlockedToolsWithReasons() {
    val payload =
        """
          {
            "toolAvailability": {
              "device_status": {
                "available": true
              },
              "vibrate": {
                "available": false,
                "reason": "vibration_unsupported"
              },
              "web_search": {
                "available": false,
                "reason": "device_bridge_missing"
              }
            }
          }
        """.trimIndent()

    assertEquals(
        "vibrate (vibration_unsupported), web_search (device_bridge_missing)",
        describeToolAvailability(payload),
    )
  }

  @Test
  fun describeToolAvailabilityReportsWhenAllToolsAreReady() {
    val payload =
        """
          {
            "toolAvailability": {
              "device_status": {
                "available": true
              },
              "runtime_status": {
                "available": true
              }
            }
          }
        """.trimIndent()

    assertEquals(
        "All runtime-advertised mobile tools are ready.",
        describeToolAvailability(payload),
    )
  }

  @Test
  fun describeHostCapabilitiesListsBlockedCapabilitiesWithReasons() {
    val payload =
        """
          {
            "hostCapabilities": {
              "camera": {
                "ready": false,
                "reason": "background_gated"
              },
              "capture": {
                "ready": false,
                "reason": "background_gated"
              },
              "microphone": {
                "ready": true
              }
            }
          }
        """.trimIndent()

    assertEquals(
        "camera (background_gated), capture (background_gated)",
        describeHostCapabilities(payload),
    )
  }

  @Test
  fun describeDeviceStatusIncludesBlockedHostCapabilitiesSummary() {
    val payload =
        """
          {
            "foreground": false,
            "serviceRunning": true,
            "captureRequested": true,
            "permissionsGranted": true,
            "microphoneStatus": "running",
            "cameraStatus": "running",
            "speakerStatus": "idle",
            "hostCapabilities": {
              "camera": {
                "ready": false,
                "reason": "background_gated"
              },
              "capture": {
                "ready": false,
                "reason": "background_gated"
              }
            }
          }
        """.trimIndent()

    assertEquals(
        "background | service on | capture on | permissions ok | mic running | cam running | speaker idle | blocked camera (background_gated), capture (background_gated)",
        describeDeviceStatus(payload),
    )
  }

  @Test
  fun describeSpeechStateSummarizesActiveCapture() {
    val payload =
        """
          {
            "speechState": {
              "available": true,
              "state": "capturing",
              "currentSegmentIndex": 2,
              "durationMs": 375
            }
          }
        """.trimIndent()

    assertEquals("speech capturing seg 2 375ms", describeSpeechState(payload))
  }

  @Test
  fun describeDeviceStatusIncludesSpeechStateSummaryWhenAvailable() {
    val payload =
        """
          {
            "foreground": true,
            "serviceRunning": true,
            "captureRequested": true,
            "permissionsGranted": true,
            "microphoneStatus": "running",
            "cameraStatus": "running",
            "speakerStatus": "idle",
            "speechState": {
              "available": true,
              "state": "capturing",
              "currentSegmentIndex": 1,
              "durationMs": 250
            }
          }
        """.trimIndent()

    assertEquals(
        "foreground | service on | capture on | permissions ok | mic running | cam running | speaker idle | speech capturing seg 1 250ms",
        describeDeviceStatus(payload),
    )
  }

  @Test
  fun describeSpeechStateSupportsDirectSpeechEventPayload() {
    val payload =
        """
          {
            "available": true,
            "state": "capturing",
            "currentSegmentIndex": 3,
            "durationMs": 480
          }
        """.trimIndent()

    assertEquals("speech capturing seg 3 480ms", describeSpeechState(payload))
  }

  @Test
  fun speechStateSummaryOrDefaultClearsUnavailableSpeechPayloads() {
    val payload =
        """
          {
            "available": false,
            "state": "idle"
          }
        """.trimIndent()

    assertEquals("No live speech state yet.", speechStateSummaryOrDefault(payload))
  }
}
