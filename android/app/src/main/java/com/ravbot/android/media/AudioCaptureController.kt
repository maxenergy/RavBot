package com.ravbot.android.media

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Handler
import android.os.Looper
import com.ravbot.android.bridge.RavbotNativeBridge
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs
import kotlin.math.max

class AudioCaptureController(
    private val bridge: RavbotNativeBridge,
    private val sampleRateHz: Int = 16000,
    private val onStateChanged: (String) -> Unit = {},
    private val onSpeechLevelChanged: (Float) -> Unit = {},
    private val onSpeechStarted: () -> Unit = {},
) {
  private val mainHandler = Handler(Looper.getMainLooper())
  private val executor = Executors.newSingleThreadExecutor()
  private val running = AtomicBoolean(false)

  @Volatile private var audioRecord: AudioRecord? = null

  @Volatile private var speechActive = false

  @Volatile private var silentChunkCount = 0

  @SuppressLint("MissingPermission")
  fun start(): Boolean {
    if (running.get()) {
      postState("running")
      return true
    }

    val minBufferSize =
        AudioRecord.getMinBufferSize(
            sampleRateHz,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
    if (minBufferSize <= 0) {
      postState("error: microphone buffer unavailable")
      return false
    }

    val samplesPerChunk = max(320, sampleRateHz / 10)
    val frameBytes = samplesPerChunk * 2
    val audio =
        AudioRecord.Builder()
            .setAudioSource(MediaRecorder.AudioSource.VOICE_RECOGNITION)
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(sampleRateHz)
                    .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                    .build(),
            )
            .setBufferSizeInBytes(max(minBufferSize, frameBytes * 4))
            .build()
    if (audio.state != AudioRecord.STATE_INITIALIZED) {
      audio.release()
      postState("error: microphone init failed")
      return false
    }

    speechActive = false
    silentChunkCount = 0
    audioRecord = audio
    running.set(true)
    postState("starting")
    executor.execute {
      captureLoop(audio, samplesPerChunk)
    }
    return true
  }

  fun stop() {
    if (!running.getAndSet(false)) {
      postState("stopped")
      return
    }

    flushActiveSpeechTurn()
    audioRecord?.let { audio ->
      runCatching { audio.stop() }
    }
    postLevel(0f)
    postState("stopped")
  }

  fun dispose() {
    stop()
    executor.shutdownNow()
  }

  private fun captureLoop(audio: AudioRecord, samplesPerChunk: Int) {
    try {
      if (!running.get()) {
        return
      }
      audio.startRecording()
      postState("running")
      val chunk = ShortArray(samplesPerChunk)
      while (running.get()) {
        val read =
            audio.read(
                chunk,
                0,
                chunk.size,
                AudioRecord.READ_BLOCKING,
            )
        if (read <= 0) {
          postState("error: microphone read failed ($read)")
          break
        }

        val payload = if (read == chunk.size) chunk.copyOf() else chunk.copyOf(read)
        val level = averageLevel(payload)
        postLevel((level / Short.MAX_VALUE.toFloat()).coerceIn(0f, 1f))

        if (level >= kSpeechThreshold) {
          if (!speechActive) {
            postSpeechStarted()
          }
          speechActive = true
          silentChunkCount = 0
          bridge.pushPcm16(
              samples = payload,
              sampleRateHz = sampleRateHz,
              channels = 1,
              endOfTurn = false,
          )
          continue
        }

        if (!speechActive) {
          continue
        }

        silentChunkCount += 1
        val endOfTurn = silentChunkCount >= kSilenceChunkLimit
        bridge.pushPcm16(
            samples = payload,
            sampleRateHz = sampleRateHz,
            channels = 1,
            endOfTurn = endOfTurn,
        )
        if (endOfTurn) {
          speechActive = false
          silentChunkCount = 0
          postState("running: awaiting speech")
        }
      }
    } catch (error: Exception) {
      flushActiveSpeechTurn()
      postState("error: ${error.message ?: "microphone failure"}")
    } finally {
      running.set(false)
      audioRecord = null
      runCatching { audio.stop() }
      audio.release()
      postLevel(0f)
      postState("stopped")
    }
  }

  private fun averageLevel(samples: ShortArray): Float {
    if (samples.isEmpty()) {
      return 0f
    }

    var sum = 0L
    for (sample in samples) {
      sum += abs(sample.toInt())
    }
    return sum.toFloat() / samples.size.toFloat()
  }

  private fun postState(value: String) {
    mainHandler.post {
      onStateChanged(value)
    }
  }

  private fun postLevel(value: Float) {
    mainHandler.post {
      onSpeechLevelChanged(value)
    }
  }

  private fun postSpeechStarted() {
    mainHandler.post {
      onSpeechStarted()
    }
  }

  private fun flushActiveSpeechTurn() {
    if (!speechActive) {
      return
    }
    speechActive = false
    silentChunkCount = 0
    bridge.flushAudioTurn()
    postState("running: speech flushed")
  }

  companion object {
    private const val kSpeechThreshold = 900f
    private const val kSilenceChunkLimit = 7
  }
}
