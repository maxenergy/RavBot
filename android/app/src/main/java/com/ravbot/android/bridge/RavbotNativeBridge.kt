package com.ravbot.android.bridge

import android.os.Handler
import android.os.Looper
import com.ravbot.android.network.HostWebToolClient
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject

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
  private val webToolClient = HostWebToolClient()
  @Volatile private var hostExecutor: ExecutorService = Executors.newSingleThreadExecutor()
  private var hostWebSearchEnabled: Boolean = true
  private var hostWebFetchEnabled: Boolean = true
  private var hostHapticsEnabled: Boolean = false
  @Volatile
  private var captureControlHandler: ((String, Boolean) -> String)? = null

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

    ensureHostExecutor()
    engineHandle = nativeInitEngine(configJson, stateDir, modelsDir)
    if (engineHandle != 0L && eventListener != null) {
      nativeSubscribeEvents(engineHandle)
    }
    if (engineHandle != 0L) {
      nativeSetHostWebCapabilities(
          engineHandle,
          hostWebSearchEnabled,
          hostWebFetchEnabled,
      )
      nativeSetCaptureControlCapability(
          engineHandle,
          captureControlHandler != null,
      )
      nativeSetHapticsCapability(engineHandle, hostHapticsEnabled)
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
      timestampMs: Long,
      pixels: ByteArray,
  ): Boolean {
    val handle = engineHandle.takeIf { it != 0L } ?: return false
    return nativePushCameraFrame(
        handle,
        width,
        height,
        stride,
        format,
        timestampMs,
        pixels,
    )
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
      hostWebSearchEnabled: Boolean,
      hostWebFetchEnabled: Boolean,
      hostHapticsEnabled: Boolean,
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
        hostWebSearchEnabled,
        hostWebFetchEnabled,
        hostHapticsEnabled,
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
    nativeSetHostWebCapabilities(handle, hostWebSearchEnabled, hostWebFetchEnabled)
    nativeSetCaptureControlCapability(handle, captureControlHandler != null)
    nativeSetHapticsCapability(handle, hostHapticsEnabled)
  }

  fun setHostWebToolsEnabled(
      webSearchEnabled: Boolean,
      webFetchEnabled: Boolean,
  ) {
    hostWebSearchEnabled = webSearchEnabled
    hostWebFetchEnabled = webFetchEnabled

    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeSetHostWebCapabilities(handle, webSearchEnabled, webFetchEnabled)
  }

  fun setHapticsEnabled(enabled: Boolean) {
    hostHapticsEnabled = enabled
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeSetHapticsCapability(handle, enabled)
  }

  fun setCaptureControlHandler(handler: ((String, Boolean) -> String)?) {
    captureControlHandler = handler
    val handle = engineHandle.takeIf { it != 0L } ?: return
    nativeSetCaptureControlCapability(handle, handler != null)
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
    captureControlHandler = null
    if (engineHandle != 0L) {
      nativeFreeEngine(engineHandle)
      engineHandle = 0L
    }
    hostExecutor.shutdownNow()
  }

  @Suppress("unused")
  fun onNativeEvent(name: String, payload: String) {
    val event = NativeEvent(name = name, payload = payload)
    mainHandler.post {
      eventListener?.invoke(event)
    }
  }

  @Suppress("unused")
  fun onNativeWebSearch(
      sessionId: String,
      query: String,
      count: Int,
      freshness: String,
  ): String =
      ensureHostExecutor()
          .submit<String> {
            webToolClient.webSearch(
                sessionId = sessionId,
                query = query,
                count = count,
                freshness = freshness.ifBlank { null },
            )
          }
          .get()

  @Suppress("unused")
  fun onNativeWebFetch(
      sessionId: String,
      url: String,
      maxChars: Int,
  ): String =
      ensureHostExecutor().submit<String> {
        webToolClient.webFetch(sessionId, url, maxChars)
      }.get()

  @Suppress("unused")
  fun onNativeCaptureControl(
      sessionId: String,
      enabled: Boolean,
  ): String {
    val handler = captureControlHandler
    if (handler == null) {
      return captureControlResponse(
          accepted = false,
          requested = enabled,
          reason = "capture_control_unavailable",
          detail = "Android host has no capture control handler registered.",
      )
    }

    if (Looper.myLooper() == Looper.getMainLooper()) {
      return invokeCaptureControlHandler(handler, sessionId, enabled)
    }

    val latch = CountDownLatch(1)
    val resultRef = AtomicReference<String>()
    mainHandler.post {
      resultRef.set(invokeCaptureControlHandler(handler, sessionId, enabled))
      latch.countDown()
    }
    if (!latch.await(5, TimeUnit.SECONDS)) {
      return captureControlResponse(
          accepted = false,
          requested = enabled,
          reason = "capture_control_timeout",
          detail = "Timed out waiting for the Android host capture control handler.",
      )
    }
    return resultRef.get()
        ?: captureControlResponse(
            accepted = false,
            requested = enabled,
            reason = "capture_control_unavailable",
            detail = "Android host capture control handler returned no result.",
        )
  }

  @Synchronized
  private fun ensureHostExecutor(): ExecutorService {
    if (hostExecutor.isShutdown || hostExecutor.isTerminated) {
      hostExecutor = Executors.newSingleThreadExecutor()
    }
    return hostExecutor
  }

  private fun invokeCaptureControlHandler(
      handler: (String, Boolean) -> String,
      sessionId: String,
      enabled: Boolean,
  ): String {
    return runCatching {
          handler(sessionId, enabled)
        }
        .fold(
            onSuccess = { result ->
              if (result.isBlank()) {
                captureControlResponse(
                    accepted = true,
                    requested = enabled,
                    detail =
                        if (enabled) {
                          "Android host accepted live capture start request."
                        } else {
                          "Android host accepted live capture stop request."
                        },
                )
              } else {
                result
              }
            },
            onFailure = { error ->
              captureControlResponse(
                  accepted = false,
                  requested = enabled,
                  reason = "capture_control_exception",
                  detail = error.message ?: "Android host capture control failed.",
              )
            },
        )
  }

  private fun captureControlResponse(
      accepted: Boolean,
      requested: Boolean,
      reason: String? = null,
      detail: String,
  ): String {
    val json =
        JSONObject()
            .put("accepted", accepted)
            .put("requested", requested)
            .put("detail", detail)
    if (!reason.isNullOrBlank()) {
      json.put("reason", reason)
    }
    return json.toString()
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
      timestampMs: Long,
      pixels: ByteArray,
  ): Boolean

  private external fun nativeInterruptGeneration(handle: Long)

  private external fun nativeReportTtsState(handle: Long, state: String)

  private external fun nativeReportDeviceStatus(
      handle: Long,
      serviceRunning: Boolean,
      captureRequested: Boolean,
      permissionsGranted: Boolean,
      hostWebSearchEnabled: Boolean,
      hostWebFetchEnabled: Boolean,
      hostHapticsEnabled: Boolean,
      microphoneStatus: String,
      cameraStatus: String,
      speakerStatus: String,
  )

  private external fun nativeSetForegroundState(handle: Long, isForeground: Boolean)

  private external fun nativeSubscribeEvents(handle: Long)

  private external fun nativeUnsubscribeEvents(handle: Long)

  private external fun nativeSetHostWebCapabilities(
      handle: Long,
      webSearchEnabled: Boolean,
      webFetchEnabled: Boolean,
  )

  private external fun nativeSetHapticsCapability(
      handle: Long,
      enabled: Boolean,
  )

  private external fun nativeSetCaptureControlCapability(
      handle: Long,
      enabled: Boolean,
  )

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
