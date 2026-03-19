package com.ravbot.android.ui

import org.junit.Assert.assertEquals
import org.junit.Test

class RuntimeStatusParsingTest {
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
}
