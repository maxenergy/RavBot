package com.ravbot.android.media

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.speech.tts.TextToSpeech
import android.speech.tts.UtteranceProgressListener
import java.util.Locale
import java.util.UUID
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.sin

class SpeechPlaybackController(
    context: Context,
    private val onStateChanged: (String) -> Unit = {},
    private val onSpeakingChanged: (Boolean) -> Unit = {},
    private val onEnvelopeChanged: (Float) -> Unit = {},
    private val onPlaybackEvent: (String) -> Unit = {},
) : TextToSpeech.OnInitListener {
  private val mainHandler = Handler(Looper.getMainLooper())
  private val appContext = context.applicationContext

  @Volatile private var textToSpeech: TextToSpeech? = null
  @Volatile private var ready = false
  @Volatile private var pendingText: String? = null
  @Volatile private var activeSpeechLength = 0
  @Volatile private var envelopeTick = 0

  private val envelopeRunnable =
      object : Runnable {
        override fun run() {
          if (!isSpeaking()) {
            stopEnvelopeAnimation()
            return
          }

          val cadence = min(12, maxOf(4, activeSpeechLength / 10 + 4))
          val pulse =
              abs(sin((envelopeTick % cadence).toDouble() / cadence.toDouble() * Math.PI))
          val accent = if (envelopeTick % 4 == 0) 0.92 else 0.72
          postEnvelope((0.18 + pulse * accent).toFloat().coerceIn(0f, 1f))
          envelopeTick += 1
          mainHandler.postDelayed(this, 90L)
        }
      }

  init {
    textToSpeech = TextToSpeech(appContext, this)
    postState("initializing")
    postEnvelope(0f)
  }

  override fun onInit(status: Int) {
    val tts = textToSpeech ?: return
    if (status != TextToSpeech.SUCCESS) {
      ready = false
      postState("error: tts init failed ($status)")
      return
    }

    ready = true
    tts.setOnUtteranceProgressListener(
        object : UtteranceProgressListener() {
          override fun onStart(utteranceId: String?) {
            postState("speaking")
            postSpeaking(true)
            startEnvelopeAnimation()
            postPlaybackEvent("speaking")
          }

          override fun onDone(utteranceId: String?) {
            stopEnvelopeAnimation()
            postState("idle")
            postSpeaking(false)
            postPlaybackEvent("completed")
          }

          @Deprecated("Deprecated in Java")
          override fun onError(utteranceId: String?) {
            stopEnvelopeAnimation()
            postState("error: tts playback failed")
            postSpeaking(false)
            postPlaybackEvent("error")
          }

          override fun onError(utteranceId: String?, errorCode: Int) {
            stopEnvelopeAnimation()
            postState("error: tts playback failed ($errorCode)")
            postSpeaking(false)
            postPlaybackEvent("error")
          }
        },
    )
    postState("ready")

    pendingText?.let { text ->
      pendingText = null
      speak(text)
    }
  }

  fun speak(text: String) {
    if (text.isBlank()) {
      return
    }

    val tts = textToSpeech
    if (!ready || tts == null) {
      pendingText = text
      postState("queued")
      return
    }

    val locale = pickLocale(text)
    val result = tts.setLanguage(locale)
    if (result == TextToSpeech.LANG_MISSING_DATA ||
        result == TextToSpeech.LANG_NOT_SUPPORTED) {
      tts.setLanguage(Locale.US)
    }
    activeSpeechLength = text.length
    envelopeTick = 0
    val utteranceId = "ravbot-${UUID.randomUUID()}"
    val speakResult = tts.speak(text, TextToSpeech.QUEUE_FLUSH, null, utteranceId)
    if (speakResult != TextToSpeech.SUCCESS) {
      stopEnvelopeAnimation()
      postState("error: tts speak failed ($speakResult)")
      postPlaybackEvent("error")
    }
  }

  fun stop() {
    pendingText = null
    val wasSpeaking = textToSpeech?.isSpeaking == true
    textToSpeech?.stop()
    stopEnvelopeAnimation()
    postState("idle")
    postSpeaking(false)
    if (wasSpeaking) {
      postPlaybackEvent("interrupted")
    }
  }

  fun isSpeaking(): Boolean = textToSpeech?.isSpeaking == true

  fun dispose() {
    stop()
    textToSpeech?.shutdown()
    textToSpeech = null
    ready = false
  }

  private fun pickLocale(text: String): Locale {
    return if (text.any { char -> char.code in 0x4E00..0x9FFF }) {
      Locale.SIMPLIFIED_CHINESE
    } else {
      Locale.US
    }
  }

  private fun postState(value: String) {
    mainHandler.post {
      onStateChanged(value)
    }
  }

  private fun postSpeaking(value: Boolean) {
    mainHandler.post {
      onSpeakingChanged(value)
    }
  }

  private fun postPlaybackEvent(value: String) {
    mainHandler.post {
      onPlaybackEvent(value)
    }
  }

  private fun postEnvelope(value: Float) {
    mainHandler.post {
      onEnvelopeChanged(value)
    }
  }

  private fun startEnvelopeAnimation() {
    mainHandler.removeCallbacks(envelopeRunnable)
    envelopeTick = 0
    postEnvelope(0.35f)
    mainHandler.post(envelopeRunnable)
  }

  private fun stopEnvelopeAnimation() {
    mainHandler.removeCallbacks(envelopeRunnable)
    envelopeTick = 0
    postEnvelope(0f)
  }
}
