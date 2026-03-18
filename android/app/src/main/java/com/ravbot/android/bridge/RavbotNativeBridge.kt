package com.ravbot.android.bridge

import android.os.Handler
import android.os.Looper

data class NativeLoadStatus(
    val isLoaded: Boolean,
    val detail: String,
)

data class NativeEvent(
    val name: String,
    val payload: String,
    val timestampMillis: Long = System.currentTimeMillis(),
)

class RavbotNativeBridge {
  private var engineHandle: Long = 0L
  private var eventListener: ((NativeEvent) -> Unit)? = null
  private val mainHandler = Handler(Looper.getMainLooper())

  val loadStatus: NativeLoadStatus
    get() = sharedLoadStatus

  fun isInitialized(): Boolean = engineHandle != 0L

  fun initEngine(
      configJson: String,
      stateDir: String,
      modelsDir: String,
  ): Boolean {
    if (!loadStatus.isLoaded) {
      return false
    }

    if (engineHandle != 0L) {
      nativeFreeEngine(engineHandle)
      engineHandle = 0L
    }

    engineHandle = nativeInitEngine(configJson, stateDir, modelsDir)
    if (engineHandle != 0L && eventListener != null) {
      nativeSubscribeEvents(engineHandle)
    }
    return engineHandle != 0L
  }

  fun startSession(sessionId: String): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativeStartSession(handle, sessionId)
  }

  fun sendTextTurn(text: String): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativeSendTextTurn(handle, text)
  }

  fun pushPcm16(
      samples: ShortArray,
      sampleRateHz: Int,
      channels: Int,
      endOfTurn: Boolean = true,
  ): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativePushPcm16(handle, samples, sampleRateHz, channels, endOfTurn)
  }

  fun flushAudioTurn(): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativeFlushAudioTurn(handle)
  }

  fun pushCameraFrame(
      width: Int,
      height: Int,
      stride: Int,
      format: String,
      pixels: ByteArray,
  ): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativePushCameraFrame(handle, width, height, stride, format, pixels)
  }

  fun interruptGeneration() {
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeInterruptGeneration(handle)
  }

  fun reportTtsState(state: String) {
    val handle = engineHandle.takeIf { it != 0L } ?: return
    if (state.isBlank()) {
      return
    }
    nativeReportTtsState(handle, state)
  }

  fun reportDeviceStatus(
      serviceRunning: Boolean,
      captureRequested: Boolean,
      permissionsGranted: Boolean,
      microphoneStatus: String,
      cameraStatus: String,
      speakerStatus: String,
  ) {
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeReportDeviceStatus(
        handle,
        serviceRunning,
        captureRequested,
        permissionsGranted,
        microphoneStatus,
        cameraStatus,
        speakerStatus,
    )
  }

  fun setForegroundState(isForeground: Boolean) {
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeSetForegroundState(handle, isForeground)
  }

  fun subscribeEvents(listener: (NativeEvent) -> Unit) {
    eventListener = listener
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeSubscribeEvents(handle)
  }

  fun unsubscribeEvents() {
    val handle = engineHandle.takeIf { it != 0L }
    if (handle != null) {
      nativeUnsubscribeEvents(handle)
    }
    eventListener = null
  }

  fun dispose() {
    unsubscribeEvents()
    if (engineHandle != 0L) {
      nativeFreeEngine(engineHandle)
      engineHandle = 0L
    }
  }

  @Suppress("unused")
  fun onNativeEvent(name: String, payload: String) {
    val event = NativeEvent(name = name, payload = payload)
    mainHandler.post {
      eventListener?.invoke(event)
    }
  }

  private external fun nativeInitEngine(
      configJson: String,
      stateDir: String,
      modelsDir: String,
  ): Long

  private external fun nativeFreeEngine(handle: Long)

  private external fun nativeStartSession(handle: Long, sessionId: String): Boolean

  private external fun nativeSendTextTurn(handle: Long, text: String): Boolean

  private external fun nativePushPcm16(
      handle: Long,
      samples: ShortArray,
      sampleRateHz: Int,
      channels: Int,
      endOfTurn: Boolean,
  ): Boolean

  private external fun nativeFlushAudioTurn(handle: Long): Boolean

  private external fun nativePushCameraFrame(
      handle: Long,
      width: Int,
      height: Int,
      stride: Int,
      format: String,
      pixels: ByteArray,
  ): Boolean

  private external fun nativeInterruptGeneration(handle: Long)

  private external fun nativeReportTtsState(handle: Long, state: String)

  private external fun nativeReportDeviceStatus(
      handle: Long,
      serviceRunning: Boolean,
      captureRequested: Boolean,
      permissionsGranted: Boolean,
      microphoneStatus: String,
      cameraStatus: String,
      speakerStatus: String,
  )

  private external fun nativeSetForegroundState(handle: Long, isForeground: Boolean)

  private external fun nativeSubscribeEvents(handle: Long)

  private external fun nativeUnsubscribeEvents(handle: Long)

  companion object {
    private val sharedLoadStatus: NativeLoadStatus by lazy {
      runCatching {
            System.loadLibrary("ravbot_mobile_android")
          }
          .fold(
              onSuccess = {
                NativeLoadStatus(
                    isLoaded = true,
                    detail = "Loaded ravbot_mobile_android with embedded ravbot_mobile_core.",
                )
              },
              onFailure = { error ->
                NativeLoadStatus(
                    isLoaded = false,
                    detail = error.message ?: "Could not load ravbot_mobile_android.",
                )
              },
          )
    }
  }
}
