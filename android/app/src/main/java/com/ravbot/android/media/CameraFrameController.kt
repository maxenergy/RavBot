package com.ravbot.android.media

import android.annotation.SuppressLint
import android.content.Context
import android.os.Handler
import android.os.Looper
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import com.ravbot.android.bridge.RavbotNativeBridge
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.math.abs
import kotlin.math.max

class CameraFrameController(
    private val context: Context,
    private val bridge: RavbotNativeBridge,
    private val sampleFps: Double = 0.5,
    private val sceneChangeThreshold: Double = 0.2,
    private val onStateChanged: (String) -> Unit = {},
) {
  private val mainHandler = Handler(Looper.getMainLooper())
  private val analyzerExecutor: ExecutorService = Executors.newSingleThreadExecutor()

  @Volatile private var running = false
  @Volatile private var cameraProvider: ProcessCameraProvider? = null
  @Volatile private var imageAnalysis: ImageAnalysis? = null
  @Volatile private var lastSubmittedAtMs = 0L
  @Volatile private var lastSceneValue = Double.NaN

  @SuppressLint("MissingPermission")
  fun start(lifecycleOwner: LifecycleOwner): Boolean {
    if (running) {
      postState("running")
      return true
    }

    running = true
    postState("starting")
    val providerFuture = ProcessCameraProvider.getInstance(context)
    providerFuture.addListener(
        {
          if (!running) {
            return@addListener
          }

          try {
            val provider = providerFuture.get()
            val useCase =
                ImageAnalysis.Builder()
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                    .build()
            useCase.setAnalyzer(analyzerExecutor) { image ->
              analyzeFrame(image)
            }

            provider.unbindAll()
            provider.bindToLifecycle(
                lifecycleOwner,
                selectCamera(provider),
                useCase,
            )
            cameraProvider = provider
            imageAnalysis = useCase
            lastSubmittedAtMs = 0L
            lastSceneValue = Double.NaN
            postState("running")
          } catch (error: Exception) {
            running = false
            postState("error: ${error.message ?: "camera bind failed"}")
          }
        },
        ContextCompat.getMainExecutor(context),
    )
    return true
  }

  fun stop() {
    if (!running) {
      postState("stopped")
      return
    }

    running = false
    imageAnalysis?.clearAnalyzer()
    imageAnalysis = null
    cameraProvider?.unbindAll()
    cameraProvider = null
    lastSubmittedAtMs = 0L
    lastSceneValue = Double.NaN
    postState("stopped")
  }

  fun dispose() {
    stop()
    analyzerExecutor.shutdownNow()
  }

  private fun analyzeFrame(image: ImageProxy) {
    try {
      if (!running) {
        return
      }

      val intervalMs = max(250L, (1000.0 / sampleFps).toLong())
      val now = System.currentTimeMillis()
      if (now - lastSubmittedAtMs < intervalMs) {
        return
      }

      val luminance = sampleAverageLuma(image)
      if (!lastSceneValue.isNaN()) {
        val delta = abs(luminance - lastSceneValue)
        if (delta < sceneChangeThreshold) {
          return
        }
      }

      val pixels = extractLumaPlane(image)
      if (pixels.isEmpty()) {
        return
      }

      val pushed =
          bridge.pushCameraFrame(
              width = image.width,
              height = image.height,
              stride = image.width,
              format = "YUV420_LUMA",
              pixels = pixels,
          )
      if (pushed) {
        lastSubmittedAtMs = now
        lastSceneValue = luminance
        postState(
            "running: ${image.width}x${image.height}, scene=" +
                String.format("%.2f", luminance),
        )
      }
    } catch (error: Exception) {
      postState("error: ${error.message ?: "camera analysis failed"}")
    } finally {
      image.close()
    }
  }

  private fun selectCamera(provider: ProcessCameraProvider): CameraSelector {
    val front =
        CameraSelector.Builder()
            .requireLensFacing(CameraSelector.LENS_FACING_FRONT)
            .build()
    return if (provider.hasCamera(front)) {
      front
    } else {
      CameraSelector.DEFAULT_BACK_CAMERA
    }
  }

  private fun sampleAverageLuma(image: ImageProxy): Double {
    val plane = image.planes.firstOrNull() ?: return 0.0
    val buffer = plane.buffer
    val rowStride = plane.rowStride
    val pixelStride = plane.pixelStride
    if (rowStride <= 0 || pixelStride <= 0) {
      return 0.0
    }

    val width = image.width
    val height = image.height
    val rowStep = max(1, height / 24)
    val columnStep = max(1, width / 24)
    var total = 0L
    var count = 0
    for (y in 0 until height step rowStep) {
      val rowOffset = y * rowStride
      for (x in 0 until width step columnStep) {
        val index = rowOffset + x * pixelStride
        if (index >= buffer.limit()) {
          continue
        }
        total += buffer.get(index).toInt() and 0xFF
        count += 1
      }
    }
    if (count == 0) {
      return 0.0
    }
    return total.toDouble() / (255.0 * count.toDouble())
  }

  private fun extractLumaPlane(image: ImageProxy): ByteArray {
    val plane = image.planes.firstOrNull() ?: return byteArrayOf()
    val width = image.width
    val height = image.height
    val pixels = ByteArray(width * height)
    copyPlane(
        buffer = plane.buffer,
        rowStride = plane.rowStride,
        pixelStride = plane.pixelStride,
        width = width,
        height = height,
        destination = pixels,
    )
    return pixels
  }

  private fun copyPlane(
      buffer: ByteBuffer,
      rowStride: Int,
      pixelStride: Int,
      width: Int,
      height: Int,
      destination: ByteArray,
  ) {
    for (y in 0 until height) {
      val rowOffset = y * rowStride
      val outputOffset = y * width
      for (x in 0 until width) {
        val index = rowOffset + x * pixelStride
        destination[outputOffset + x] =
            if (index < buffer.limit()) {
              buffer.get(index)
            } else {
              0
            }
      }
    }
  }

  private fun postState(value: String) {
    mainHandler.post {
      onStateChanged(value)
    }
  }
}
