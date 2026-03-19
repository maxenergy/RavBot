package com.ravbot.android.media

import org.junit.Assert.assertEquals
import org.junit.Test

class CameraFrameControllerTest {
  @Test
  fun resolveFrameTimestampMsUsesSensorClockDelta() {
    val nowEpochMs = 1_700_000_000_000L
    val nowElapsedRealtimeNs = 9_000_000_000L
    val sensorTimestampNs = 7_500_000_000L

    val resolved =
        resolveFrameTimestampMs(
            sensorTimestampNs = sensorTimestampNs,
            nowEpochMs = nowEpochMs,
            nowElapsedRealtimeNs = nowElapsedRealtimeNs,
        )

    assertEquals(nowEpochMs - 1500L, resolved)
  }

  @Test
  fun resolveFrameTimestampMsFallsBackToNowForInvalidSensorTime() {
    val nowEpochMs = 1_700_000_000_000L
    val nowElapsedRealtimeNs = 9_000_000_000L

    assertEquals(
        nowEpochMs,
        resolveFrameTimestampMs(
            sensorTimestampNs = -1L,
            nowEpochMs = nowEpochMs,
            nowElapsedRealtimeNs = nowElapsedRealtimeNs,
        ),
    )
    assertEquals(
        nowEpochMs,
        resolveFrameTimestampMs(
            sensorTimestampNs = nowElapsedRealtimeNs + 1L,
            nowEpochMs = nowEpochMs,
            nowElapsedRealtimeNs = nowElapsedRealtimeNs,
        ),
    )
  }
}
