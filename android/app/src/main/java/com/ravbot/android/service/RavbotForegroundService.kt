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
  private var hostHapticsEnabled = true
  private var runtimeHapticsStatus = "unknown"
  private var runtimeSpeechToolStatus = "unknown"
  private var runtimeCaptureControlStatus = "unknown"
  private var speechStateStatus = "No live speech state yet."
  private var runtimeForegroundStarted = false

  override fun onBind(intent: Intent?): IBinder? = null

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    if (intent == null) {
      return START_NOT_STICKY
    }

    when (intent.action) {
      ACTION_STOP -> {
        publishServiceRunningState(false)
        runtimeForegroundStarted = false
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        return START_NOT_STICKY
      }
      ACTION_UPDATE_STATUS -> {
        if (!runtimeForegroundStarted) {
          stopSelfResult(startId)
          return START_NOT_STICKY
        }
        updateSnapshot(intent)
        refreshRuntimeNotification()
      }
      else -> {
        updateSnapshot(intent)
        startRuntimeForeground()
      }
    }
    return START_NOT_STICKY
  }

  override fun onDestroy() {
    publishServiceRunningState(false)
    runtimeForegroundStarted = false
    super.onDestroy()
  }

  private fun startRuntimeForeground() {
    ensureNotificationChannel()
    startForeground(NOTIFICATION_ID, buildNotification())
    runtimeForegroundStarted = true
    publishServiceRunningState(true)
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
    return listOf(
            sessionLine,
            captureLine,
            "capture ctrl $runtimeCaptureControlStatus",
            "speech tool $runtimeSpeechToolStatus",
            "speech ${speechStateStatus.lowercase()}",
        )
        .joinToString(" | ")
  }

  private fun buildNotificationDetails(): String {
    return listOf(
            getString(R.string.foreground_service_text),
            "Session: ${if (sessionReady) "ready" else "idle"}",
            "Capture: ${if (captureRequested) "requested" else "stopped"}",
            "Capture control: $runtimeCaptureControlStatus",
            "Microphone: $microphoneStatus",
            "Camera: $cameraStatus",
            "Speaker: $speakerStatus",
            "Assistant: $assistantStatus",
            "Speech state: $speechStateStatus",
            "Speech tool: $runtimeSpeechToolStatus",
            "Host web search: ${if (hostWebSearchEnabled) "enabled" else "disabled"}",
            "Host web fetch: ${if (hostWebFetchEnabled) "enabled" else "disabled"}",
            "Host haptics: ${if (hostHapticsEnabled) "enabled" else "disabled"}",
            "Haptics runtime: $runtimeHapticsStatus",
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
    if (intent.hasExtra(EXTRA_HOST_HAPTICS_ENABLED)) {
      hostHapticsEnabled = intent.getBooleanExtra(EXTRA_HOST_HAPTICS_ENABLED, true)
    }
    intent.getStringExtra(EXTRA_RUNTIME_HAPTICS_STATUS)?.let {
      runtimeHapticsStatus = it
    }
    intent.getStringExtra(EXTRA_RUNTIME_SPEECH_TOOL_STATUS)?.let {
      runtimeSpeechToolStatus = it
    }
    intent.getStringExtra(EXTRA_RUNTIME_CAPTURE_CONTROL_STATUS)?.let {
      runtimeCaptureControlStatus = it
    }
    intent.getStringExtra(EXTRA_SPEECH_STATE_STATUS)?.let {
      speechStateStatus = it
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

  private fun publishServiceRunningState(running: Boolean) {
    persistRunningState(applicationContext, running)
    sendBroadcast(createServiceStateChangedIntent(applicationContext, running))
  }

  companion object {
    const val ACTION_START = "com.ravbot.android.action.START_RUNTIME"
    const val ACTION_STOP = "com.ravbot.android.action.STOP_RUNTIME"
    const val ACTION_UPDATE_STATUS = "com.ravbot.android.action.UPDATE_RUNTIME_STATUS"
    const val ACTION_SERVICE_STATE_CHANGED =
        "com.ravbot.android.action.RUNTIME_SERVICE_STATE_CHANGED"

    const val EXTRA_SESSION_READY = "session_ready"
    const val EXTRA_CAPTURE_REQUESTED = "capture_requested"
    const val EXTRA_MICROPHONE_STATUS = "microphone_status"
    const val EXTRA_CAMERA_STATUS = "camera_status"
    const val EXTRA_SPEAKER_STATUS = "speaker_status"
    const val EXTRA_ASSISTANT_STATUS = "assistant_status"
    const val EXTRA_HOST_WEB_SEARCH_ENABLED = "host_web_search_enabled"
    const val EXTRA_HOST_WEB_FETCH_ENABLED = "host_web_fetch_enabled"
    const val EXTRA_HOST_HAPTICS_ENABLED = "host_haptics_enabled"
    const val EXTRA_RUNTIME_HAPTICS_STATUS = "runtime_haptics_status"
    const val EXTRA_RUNTIME_SPEECH_TOOL_STATUS = "runtime_speech_tool_status"
    const val EXTRA_RUNTIME_CAPTURE_CONTROL_STATUS = "runtime_capture_control_status"
    const val EXTRA_SPEECH_STATE_STATUS = "speech_state_status"
    const val EXTRA_SERVICE_RUNNING = "service_running"

    private const val CHANNEL_ID = "ravbot.runtime"
    private const val NOTIFICATION_ID = 1001
    private const val REQUEST_OPEN_APP = 1002
    private const val REQUEST_STOP_SERVICE = 1003
    private const val STATE_PREFERENCES_NAME = "ravbot_foreground_service"
    private const val KEY_SERVICE_RUNNING = "service_running"

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
        hostHapticsEnabled: Boolean = true,
        runtimeHapticsStatus: String = "unknown",
        runtimeSpeechToolStatus: String = "unknown",
        runtimeCaptureControlStatus: String = "unknown",
        speechStateStatus: String = "No live speech state yet.",
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
          .putExtra(EXTRA_HOST_HAPTICS_ENABLED, hostHapticsEnabled)
          .putExtra(EXTRA_RUNTIME_HAPTICS_STATUS, runtimeHapticsStatus)
          .putExtra(EXTRA_RUNTIME_SPEECH_TOOL_STATUS, runtimeSpeechToolStatus)
          .putExtra(
              EXTRA_RUNTIME_CAPTURE_CONTROL_STATUS,
              runtimeCaptureControlStatus,
          )
          .putExtra(EXTRA_SPEECH_STATE_STATUS, speechStateStatus)
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
        hostHapticsEnabled: Boolean,
        runtimeHapticsStatus: String,
        runtimeSpeechToolStatus: String,
        runtimeCaptureControlStatus: String,
        speechStateStatus: String,
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
          .putExtra(EXTRA_HOST_HAPTICS_ENABLED, hostHapticsEnabled)
          .putExtra(EXTRA_RUNTIME_HAPTICS_STATUS, runtimeHapticsStatus)
          .putExtra(EXTRA_RUNTIME_SPEECH_TOOL_STATUS, runtimeSpeechToolStatus)
          .putExtra(
              EXTRA_RUNTIME_CAPTURE_CONTROL_STATUS,
              runtimeCaptureControlStatus,
          )
          .putExtra(EXTRA_SPEECH_STATE_STATUS, speechStateStatus)
    }

    fun createServiceStateChangedIntent(
        context: Context,
        running: Boolean,
    ): Intent {
      return Intent(ACTION_SERVICE_STATE_CHANGED)
          .setPackage(context.packageName)
          .putExtra(EXTRA_SERVICE_RUNNING, running)
    }

    internal fun loadPersistedRunningState(context: Context): Boolean {
      return context
          .getSharedPreferences(STATE_PREFERENCES_NAME, Context.MODE_PRIVATE)
          .getBoolean(KEY_SERVICE_RUNNING, false)
    }

    internal fun persistRunningState(context: Context, running: Boolean) {
      context
          .getSharedPreferences(STATE_PREFERENCES_NAME, Context.MODE_PRIVATE)
          .edit()
          .putBoolean(KEY_SERVICE_RUNNING, running)
          .apply()
    }
  }
}
