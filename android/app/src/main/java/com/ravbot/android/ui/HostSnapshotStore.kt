package com.ravbot.android.ui

import android.content.Context
import org.json.JSONObject

data class HostSnapshot(
    val sessionId: String = "android-mvp",
    val promptText: String = "What can you see and hear right now?",
    val avatarState: String = "idle",
    val restoreEngine: Boolean = false,
    val restoreSession: Boolean = false,
    val restoreService: Boolean = false,
    val restoreCapture: Boolean = false,
    val microphoneStatus: String = "stopped",
    val cameraStatus: String = "stopped",
    val speakerStatus: String = "idle",
    val assistantStatus: String = "idle",
    val lastAsrText: String = "No speech yet.",
    val lastAsrStats: String = "No ASR metadata yet.",
    val lastVisionSummary: String = "No scene observations yet.",
    val lastAssistantReply: String = "No assistant reply yet.",
    val lastToolActivity: String = "No tool activity yet.",
    val runtimeProvider: String = "unknown",
    val runtimeDetail: String = "No native runtime diagnostics emitted yet.",
    val runtimeModelsDir: String = "not set",
    val runtimeVisionProvider: String = "unknown",
    val runtimeVisionDetail: String = "No native vision diagnostics emitted yet.",
    val runtimeAvailableTools: String = "No mobile-safe tools advertised yet.",
    val runtimeToolAvailability: String =
        "No per-tool availability diagnostics emitted yet.",
    val hostWebSearchEnabled: Boolean = true,
    val hostWebFetchEnabled: Boolean = true,
    val hostHapticsEnabled: Boolean = true,
    val runtimeDeviceBridgeStatus: String = "unknown",
    val runtimeWebSearchStatus: String = "unknown",
    val runtimeWebFetchStatus: String = "unknown",
    val runtimeHapticsStatus: String = "unknown",
    val runtimeTextStatus: String = "unknown",
    val runtimeVisionStatus: String = "unknown",
    val runtimeVulkanStatus: String = "unknown",
    val runtimeLlmModel: String = "not resolved",
    val runtimeVisionModel: String = "not resolved",
    val runtimeMmprojModel: String = "not resolved",
    val speechBackendStatus: String = "unknown",
    val speechAsrStatus: String = "unknown",
    val speechTtsStatus: String = "unknown",
    val runtimeSpeechToolStatus: String = "unknown",
    val runtimeCaptureControlStatus: String = "unknown",
    val speechSttModel: String = "not resolved",
    val speechTtsVoice: String = "not resolved",
    val speechDetail: String = "No native speech diagnostics emitted yet.",
    val speechStateStatus: String = "No live speech state yet.",
) {
  fun shouldRestoreRuntime(): Boolean {
    return restoreEngine || restoreSession || restoreService || restoreCapture
  }

  fun toJson(): String {
    return JSONObject()
        .put("sessionId", sessionId)
        .put("promptText", promptText)
        .put("avatarState", avatarState)
        .put("restoreEngine", restoreEngine)
        .put("restoreSession", restoreSession)
        .put("restoreService", restoreService)
        .put("restoreCapture", restoreCapture)
        .put("microphoneStatus", microphoneStatus)
        .put("cameraStatus", cameraStatus)
        .put("speakerStatus", speakerStatus)
        .put("assistantStatus", assistantStatus)
        .put("lastAsrText", lastAsrText)
        .put("lastAsrStats", lastAsrStats)
        .put("lastVisionSummary", lastVisionSummary)
        .put("lastAssistantReply", lastAssistantReply)
        .put("lastToolActivity", lastToolActivity)
        .put("runtimeProvider", runtimeProvider)
        .put("runtimeDetail", runtimeDetail)
        .put("runtimeModelsDir", runtimeModelsDir)
        .put("runtimeVisionProvider", runtimeVisionProvider)
        .put("runtimeVisionDetail", runtimeVisionDetail)
        .put("runtimeAvailableTools", runtimeAvailableTools)
        .put("runtimeToolAvailability", runtimeToolAvailability)
        .put("hostWebSearchEnabled", hostWebSearchEnabled)
        .put("hostWebFetchEnabled", hostWebFetchEnabled)
        .put("hostHapticsEnabled", hostHapticsEnabled)
        .put("runtimeDeviceBridgeStatus", runtimeDeviceBridgeStatus)
        .put("runtimeWebSearchStatus", runtimeWebSearchStatus)
        .put("runtimeWebFetchStatus", runtimeWebFetchStatus)
        .put("runtimeHapticsStatus", runtimeHapticsStatus)
        .put("runtimeTextStatus", runtimeTextStatus)
        .put("runtimeVisionStatus", runtimeVisionStatus)
        .put("runtimeVulkanStatus", runtimeVulkanStatus)
        .put("runtimeLlmModel", runtimeLlmModel)
        .put("runtimeVisionModel", runtimeVisionModel)
        .put("runtimeMmprojModel", runtimeMmprojModel)
        .put("speechBackendStatus", speechBackendStatus)
        .put("speechAsrStatus", speechAsrStatus)
        .put("speechTtsStatus", speechTtsStatus)
        .put("runtimeSpeechToolStatus", runtimeSpeechToolStatus)
        .put("runtimeCaptureControlStatus", runtimeCaptureControlStatus)
        .put("speechSttModel", speechSttModel)
        .put("speechTtsVoice", speechTtsVoice)
        .put("speechDetail", speechDetail)
        .put("speechStateStatus", speechStateStatus)
        .toString()
  }

  companion object {
    fun fromJson(json: String?): HostSnapshot {
      if (json.isNullOrBlank()) {
        return HostSnapshot()
      }

      return runCatching {
            val parsed = JSONObject(json)
            HostSnapshot(
                sessionId = parsed.optString("sessionId", "android-mvp"),
                promptText =
                    parsed.optString(
                        "promptText",
                        "What can you see and hear right now?",
                    ),
                avatarState = parsed.optString("avatarState", "idle"),
                restoreEngine = parsed.optBoolean("restoreEngine", false),
                restoreSession = parsed.optBoolean("restoreSession", false),
                restoreService = parsed.optBoolean("restoreService", false),
                restoreCapture = parsed.optBoolean("restoreCapture", false),
                microphoneStatus = parsed.optString("microphoneStatus", "stopped"),
                cameraStatus = parsed.optString("cameraStatus", "stopped"),
                speakerStatus = parsed.optString("speakerStatus", "idle"),
                assistantStatus = parsed.optString("assistantStatus", "idle"),
                lastAsrText = parsed.optString("lastAsrText", "No speech yet."),
                lastAsrStats =
                    parsed.optString("lastAsrStats", "No ASR metadata yet."),
                lastVisionSummary =
                    parsed.optString(
                        "lastVisionSummary",
                        "No scene observations yet.",
                    ),
                lastAssistantReply =
                    parsed.optString(
                        "lastAssistantReply",
                        "No assistant reply yet.",
                    ),
                lastToolActivity =
                    parsed.optString(
                        "lastToolActivity",
                        "No tool activity yet.",
                    ),
                runtimeProvider = parsed.optString("runtimeProvider", "unknown"),
                runtimeDetail =
                    parsed.optString(
                        "runtimeDetail",
                        "No native runtime diagnostics emitted yet.",
                    ),
                runtimeModelsDir =
                    parsed.optString("runtimeModelsDir", "not set"),
                runtimeVisionProvider =
                    parsed.optString("runtimeVisionProvider", "unknown"),
                runtimeVisionDetail =
                    parsed.optString(
                        "runtimeVisionDetail",
                        "No native vision diagnostics emitted yet.",
                    ),
                runtimeAvailableTools =
                    parsed.optString(
                        "runtimeAvailableTools",
                        "No mobile-safe tools advertised yet.",
                    ),
                runtimeToolAvailability =
                    parsed.optString(
                        "runtimeToolAvailability",
                        "No per-tool availability diagnostics emitted yet.",
                    ),
                hostWebSearchEnabled =
                    parsed.optBoolean("hostWebSearchEnabled", true),
                hostWebFetchEnabled =
                    parsed.optBoolean("hostWebFetchEnabled", true),
                hostHapticsEnabled =
                    parsed.optBoolean("hostHapticsEnabled", true),
                runtimeDeviceBridgeStatus =
                    parsed.optString("runtimeDeviceBridgeStatus", "unknown"),
                runtimeWebSearchStatus =
                    parsed.optString("runtimeWebSearchStatus", "unknown"),
                runtimeWebFetchStatus =
                    parsed.optString("runtimeWebFetchStatus", "unknown"),
                runtimeHapticsStatus =
                    parsed.optString("runtimeHapticsStatus", "unknown"),
                runtimeTextStatus =
                    parsed.optString("runtimeTextStatus", "unknown"),
                runtimeVisionStatus =
                    parsed.optString("runtimeVisionStatus", "unknown"),
                runtimeVulkanStatus =
                    parsed.optString("runtimeVulkanStatus", "unknown"),
                runtimeLlmModel =
                    parsed.optString("runtimeLlmModel", "not resolved"),
                runtimeVisionModel =
                    parsed.optString("runtimeVisionModel", "not resolved"),
                runtimeMmprojModel =
                    parsed.optString("runtimeMmprojModel", "not resolved"),
                speechBackendStatus =
                    parsed.optString("speechBackendStatus", "unknown"),
                speechAsrStatus =
                    parsed.optString("speechAsrStatus", "unknown"),
                speechTtsStatus =
                    parsed.optString("speechTtsStatus", "unknown"),
                runtimeSpeechToolStatus =
                    parsed.optString("runtimeSpeechToolStatus", "unknown"),
                runtimeCaptureControlStatus =
                    parsed.optString("runtimeCaptureControlStatus", "unknown"),
                speechSttModel =
                    parsed.optString("speechSttModel", "not resolved"),
                speechTtsVoice =
                    parsed.optString("speechTtsVoice", "not resolved"),
                speechDetail =
                    parsed.optString(
                        "speechDetail",
                        "No native speech diagnostics emitted yet.",
                    ),
                speechStateStatus =
                    parsed.optString(
                        "speechStateStatus",
                        "No live speech state yet.",
                    ),
            )
          }
          .getOrDefault(HostSnapshot())
    }
  }
}

class HostSnapshotStore(context: Context) {
  private val preferences =
      context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE)

  fun load(): HostSnapshot {
    return HostSnapshot.fromJson(preferences.getString(KEY_SNAPSHOT, null))
  }

  fun save(snapshot: HostSnapshot) {
    preferences.edit().putString(KEY_SNAPSHOT, snapshot.toJson()).apply()
  }

  fun clear() {
    preferences.edit().remove(KEY_SNAPSHOT).apply()
  }

  companion object {
    private const val PREFERENCES_NAME = "ravbot_host_snapshot"
    private const val KEY_SNAPSHOT = "host_snapshot"
  }
}
