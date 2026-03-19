package com.ravbot.android.media

import android.content.Context
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager

class HapticsController(context: Context) {
  private val vibrator: Vibrator? =
      context.getSystemService(VibratorManager::class.java)?.defaultVibrator

  val isAvailable: Boolean
    get() = vibrator?.hasVibrator() == true

  fun vibrate(durationMs: Int) {
    val activeVibrator = vibrator ?: return
    if (!activeVibrator.hasVibrator()) {
      return
    }
    activeVibrator.cancel()
    activeVibrator.vibrate(
        VibrationEffect.createOneShot(
            durationMs.coerceIn(10, 5000).toLong(),
            VibrationEffect.DEFAULT_AMPLITUDE,
        ),
    )
  }

  fun stop() {
    vibrator?.cancel()
  }
}
