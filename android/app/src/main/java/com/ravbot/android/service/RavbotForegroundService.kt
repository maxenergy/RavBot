package com.ravbot.android.service

import android.app.PendingIntent
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.ravbot.android.R
import com.ravbot.android.MainActivity

class RavbotForegroundService : Service() {
  private var sessionReady = false
  private var captureRequested = false
  private var microphoneStatus = "stopped"
  private var cameraStatus = "stopped"
  private var speakerStatus = "idle"
  private var assistantStatus = "idle"
  private var hostWebSearchEnabled = true
  private var hostWebFetchEnabled = true
  private var runtimeHapticsStatus = "unknown"

  override fun onBind(intent: Intent?): IBinder? = null

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_STOP -> {
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        return START_NOT_STICKY
      }
      ACTION_UPDATE_STATUS -> {
        updateSnapshot(intent)
        refreshRuntimeNotification()
      }
      else -> {
        updateSnapshot(intent)
        startRuntimeForeground()
      }
    }
    return START_STICKY
  }

  private fun startRuntimeForeground() {
    ensureNotificationChannel()
    startForeground(NOTIFICATION_ID, buildNotification())
  }

  private fun refreshRuntimeNotification() {
    val manager =
        getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    manager.notify(NOTIFICATION_ID, buildNotification())
  }

  private fun buildNotification() =
      NotificationCompat.Builder(this, CHANNEL_ID)
          .setSmallIcon(android.R.drawable.presence_audio_online)
          .setContentTitle(getString(R.string.foreground_service_title))
          .setContentText(buildNotificationSummary())
          .setStyle(
              NotificationCompat.BigTextStyle().bigText(buildNotificationDetails()),
          )
          .setContentIntent(buildOpenAppPendingIntent())
          .addAction(
              android.R.drawable.ic_menu_view,
              getString(R.string.foreground_service_action_open),
              buildOpenAppPendingIntent(),
          )
          .addAction(
              android.R.drawable.ic_media_pause,
              getString(R.string.foreground_service_action_stop),
              buildStopPendingIntent(),
          )
          .setOnlyAlertOnce(true)
          .setOngoing(true)
          .build()

  private fun buildNotificationSummary(): String {
    val sessionLine = if (sessionReady) "session ready" else "session idle"
    val captureLine = if (captureRequested) "capture on" else "capture off"
    val webSearchLine = if (hostWebSearchEnabled) "web search on" else "web search off"
    val webFetchLine = if (hostWebFetchEnabled) "web fetch on" else "web fetch off"
    val hapticsLine =
        if (runtimeHapticsStatus == "ready") "haptics ready" else "haptics $runtimeHapticsStatus"
    return "$sessionLine | $captureLine | $webSearchLine | $webFetchLine | $hapticsLine"
  }

  private fun buildNotificationDetails(): String {
    return listOf(
            getString(R.string.foreground_service_text),
            "Session: ${if (sessionReady) "ready" else "idle"}",
            "Capture: ${if (captureRequested) "requested" else "stopped"}",
            "Microphone: $microphoneStatus",
            "Camera: $cameraStatus",
            "Speaker: $speakerStatus",
            "Assistant: $assistantStatus",
            "Host web search: ${if (hostWebSearchEnabled) "enabled" else "disabled"}",
            "Host web fetch: ${if (hostWebFetchEnabled) "enabled" else "disabled"}",
            "Haptics: $runtimeHapticsStatus",
        )
        .joinToString("\n")
  }

  private fun buildOpenAppPendingIntent(): PendingIntent {
    val intent =
        Intent(this, MainActivity::class.java).apply {
          flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
        }
    return PendingIntent.getActivity(
        this,
        REQUEST_OPEN_APP,
        intent,
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )
  }

  private fun buildStopPendingIntent(): PendingIntent {
    return PendingIntent.getService(
        this,
        REQUEST_STOP_SERVICE,
        createStopIntent(this),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )
  }

  private fun updateSnapshot(intent: Intent?) {
    if (intent == null) {
      return
    }

    if (intent.hasExtra(EXTRA_SESSION_READY)) {
      sessionReady = intent.getBooleanExtra(EXTRA_SESSION_READY, false)
    }
    if (intent.hasExtra(EXTRA_CAPTURE_REQUESTED)) {
      captureRequested = intent.getBooleanExtra(EXTRA_CAPTURE_REQUESTED, false)
    }
    intent.getStringExtra(EXTRA_MICROPHONE_STATUS)?.let { microphoneStatus = it }
    intent.getStringExtra(EXTRA_CAMERA_STATUS)?.let { cameraStatus = it }
    intent.getStringExtra(EXTRA_SPEAKER_STATUS)?.let { speakerStatus = it }
    intent.getStringExtra(EXTRA_ASSISTANT_STATUS)?.let { assistantStatus = it }
    if (intent.hasExtra(EXTRA_HOST_WEB_SEARCH_ENABLED)) {
      hostWebSearchEnabled = intent.getBooleanExtra(EXTRA_HOST_WEB_SEARCH_ENABLED, true)
    }
    if (intent.hasExtra(EXTRA_HOST_WEB_FETCH_ENABLED)) {
      hostWebFetchEnabled = intent.getBooleanExtra(EXTRA_HOST_WEB_FETCH_ENABLED, true)
    }
    intent.getStringExtra(EXTRA_RUNTIME_HAPTICS_STATUS)?.let {
      runtimeHapticsStatus = it
    }
  }

  private fun ensureNotificationChannel() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
      return
    }

    val manager =
        getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    val channel =
        NotificationChannel(
            CHANNEL_ID,
            getString(R.string.foreground_service_channel_name),
            NotificationManager.IMPORTANCE_LOW,
        )
    manager.createNotificationChannel(channel)
  }

  companion object {
    const val ACTION_START = "com.ravbot.android.action.START_RUNTIME"
    const val ACTION_STOP = "com.ravbot.android.action.STOP_RUNTIME"
    const val ACTION_UPDATE_STATUS = "com.ravbot.android.action.UPDATE_RUNTIME_STATUS"

    const val EXTRA_SESSION_READY = "session_ready"
    const val EXTRA_CAPTURE_REQUESTED = "capture_requested"
    const val EXTRA_MICROPHONE_STATUS = "microphone_status"
    const val EXTRA_CAMERA_STATUS = "camera_status"
    const val EXTRA_SPEAKER_STATUS = "speaker_status"
    const val EXTRA_ASSISTANT_STATUS = "assistant_status"
    const val EXTRA_HOST_WEB_SEARCH_ENABLED = "host_web_search_enabled"
    const val EXTRA_HOST_WEB_FETCH_ENABLED = "host_web_fetch_enabled"
    const val EXTRA_RUNTIME_HAPTICS_STATUS = "runtime_haptics_status"

    private const val CHANNEL_ID = "ravbot.runtime"
    private const val NOTIFICATION_ID = 1001
    private const val REQUEST_OPEN_APP = 1002
    private const val REQUEST_STOP_SERVICE = 1003

    fun createStartIntent(
        context: Context,
        sessionReady: Boolean = false,
        captureRequested: Boolean = false,
        microphoneStatus: String = "stopped",
        cameraStatus: String = "stopped",
        speakerStatus: String = "idle",
        assistantStatus: String = "idle",
        hostWebSearchEnabled: Boolean = true,
        hostWebFetchEnabled: Boolean = true,
        runtimeHapticsStatus: String = "unknown",
    ): Intent {
      return Intent(context, RavbotForegroundService::class.java)
          .setAction(ACTION_START)
          .putExtra(EXTRA_SESSION_READY, sessionReady)
          .putExtra(EXTRA_CAPTURE_REQUESTED, captureRequested)
          .putExtra(EXTRA_MICROPHONE_STATUS, microphoneStatus)
          .putExtra(EXTRA_CAMERA_STATUS, cameraStatus)
          .putExtra(EXTRA_SPEAKER_STATUS, speakerStatus)
          .putExtra(EXTRA_ASSISTANT_STATUS, assistantStatus)
          .putExtra(EXTRA_HOST_WEB_SEARCH_ENABLED, hostWebSearchEnabled)
          .putExtra(EXTRA_HOST_WEB_FETCH_ENABLED, hostWebFetchEnabled)
          .putExtra(EXTRA_RUNTIME_HAPTICS_STATUS, runtimeHapticsStatus)
    }

    fun createStopIntent(context: Context): Intent {
      return Intent(context, RavbotForegroundService::class.java)
          .setAction(ACTION_STOP)
    }

    fun createStatusIntent(
        context: Context,
        sessionReady: Boolean,
        captureRequested: Boolean,
        microphoneStatus: String,
        cameraStatus: String,
        speakerStatus: String,
        assistantStatus: String,
        hostWebSearchEnabled: Boolean,
        hostWebFetchEnabled: Boolean,
        runtimeHapticsStatus: String,
    ): Intent {
      return Intent(context, RavbotForegroundService::class.java)
          .setAction(ACTION_UPDATE_STATUS)
          .putExtra(EXTRA_SESSION_READY, sessionReady)
          .putExtra(EXTRA_CAPTURE_REQUESTED, captureRequested)
          .putExtra(EXTRA_MICROPHONE_STATUS, microphoneStatus)
          .putExtra(EXTRA_CAMERA_STATUS, cameraStatus)
          .putExtra(EXTRA_SPEAKER_STATUS, speakerStatus)
          .putExtra(EXTRA_ASSISTANT_STATUS, assistantStatus)
          .putExtra(EXTRA_HOST_WEB_SEARCH_ENABLED, hostWebSearchEnabled)
          .putExtra(EXTRA_HOST_WEB_FETCH_ENABLED, hostWebFetchEnabled)
          .putExtra(EXTRA_RUNTIME_HAPTICS_STATUS, runtimeHapticsStatus)
    }
  }
}
