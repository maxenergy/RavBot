#include <jni.h>

#include <android/log.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ravbot/mobile/mobile_c_api.h"

namespace {

constexpr const char* kTag = "RavbotMobileJni";
constexpr const char* kDefaultSessionId = "android-mvp";

struct EngineHandle {
  ravbot_mobile_engine_t* engine = nullptr;
  JavaVM* vm = nullptr;
  jobject bridge_ref = nullptr;
  uint64_t subscription_id = 0;
  std::string session_id = kDefaultSessionId;
  std::string state_dir;
  std::string models_dir;
  bool web_search_enabled = true;
  bool web_fetch_enabled = true;
  bool vibration_enabled = false;
};

std::string ToString(JNIEnv* env, jstring value) {
  if (value == nullptr) {
    return {};
  }

  const char* chars = env->GetStringUTFChars(value, nullptr);
  std::string result = chars != nullptr ? chars : "";
  if (chars != nullptr) {
    env->ReleaseStringUTFChars(value, chars);
  }
  return result;
}

std::vector<int16_t> ToShortVector(JNIEnv* env, jshortArray value) {
  if (value == nullptr) {
    return {};
  }

  const jsize length = env->GetArrayLength(value);
  std::vector<int16_t> result(static_cast<size_t>(length));
  if (length > 0) {
    env->GetShortArrayRegion(value, 0, length,
                             reinterpret_cast<jshort*>(result.data()));
  }
  return result;
}

std::vector<uint8_t> ToByteVector(JNIEnv* env, jbyteArray value) {
  if (value == nullptr) {
    return {};
  }

  const jsize length = env->GetArrayLength(value);
  std::vector<uint8_t> result(static_cast<size_t>(length));
  if (length > 0) {
    env->GetByteArrayRegion(value, 0, length,
                            reinterpret_cast<jbyte*>(result.data()));
  }
  return result;
}

EngineHandle* FromHandle(jlong handle) {
  return reinterpret_cast<EngineHandle*>(handle);
}

const char* ActiveSessionId(const EngineHandle* engine) {
  if (engine == nullptr || engine->session_id.empty()) {
    return kDefaultSessionId;
  }
  return engine->session_id.c_str();
}

void ForwardEventToJava(EngineHandle* engine,
                        const char* event_name,
                        const char* payload_json) {
  if (engine == nullptr || engine->vm == nullptr || engine->bridge_ref == nullptr) {
    return;
  }

  JNIEnv* env = nullptr;
  bool should_detach = false;
  const jint get_env_result = engine->vm->GetEnv(
      reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (get_env_result == JNI_EDETACHED) {
    if (engine->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
      __android_log_print(ANDROID_LOG_ERROR, kTag,
                          "Failed to attach thread for event callback");
      return;
    }
    should_detach = true;
  } else if (get_env_result != JNI_OK || env == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "Failed to get JNIEnv for event callback");
    return;
  }

  jclass bridge_class = env->GetObjectClass(engine->bridge_ref);
  if (bridge_class == nullptr) {
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return;
  }

  jmethodID callback =
      env->GetMethodID(bridge_class, "onNativeEvent",
                       "(Ljava/lang/String;Ljava/lang/String;)V");
  if (callback != nullptr) {
    jstring j_event_name = env->NewStringUTF(event_name != nullptr ? event_name : "");
    jstring j_payload =
        env->NewStringUTF(payload_json != nullptr ? payload_json : "{}");
    env->CallVoidMethod(engine->bridge_ref, callback, j_event_name, j_payload);
    env->DeleteLocalRef(j_event_name);
    env->DeleteLocalRef(j_payload);
  }

  env->DeleteLocalRef(bridge_class);
  if (should_detach) {
    engine->vm->DetachCurrentThread();
  }
}

void OnMobileEvent(const char* event_name,
                   const char* payload_json,
                   void* user_data) {
  auto* engine = static_cast<EngineHandle*>(user_data);
  ForwardEventToJava(engine, event_name, payload_json);
}

void OnDeviceAvatarState(const char* session_key,
                         const char* state,
                         void* user_data) {
  nlohmann::json payload = {{"state", state != nullptr ? state : "idle"}};
  if (session_key != nullptr && *session_key != '\0') {
    payload["sessionKey"] = session_key;
  }
  auto* engine = static_cast<EngineHandle*>(user_data);
  ForwardEventToJava(engine, "device.avatar_state", payload.dump().c_str());
}

void OnDeviceSpeechRequest(const char* session_key,
                           const char* text,
                           void* user_data) {
  nlohmann::json payload = {{"text", text != nullptr ? text : ""}};
  if (session_key != nullptr && *session_key != '\0') {
    payload["sessionKey"] = session_key;
  }
  auto* engine = static_cast<EngineHandle*>(user_data);
  ForwardEventToJava(engine, "device.speech_request", payload.dump().c_str());
}

void OnDeviceSpeechInterrupt(const char* session_key, void* user_data) {
  auto* engine = static_cast<EngineHandle*>(user_data);
  nlohmann::json payload = nlohmann::json::object();
  if (session_key != nullptr && *session_key != '\0') {
    payload["sessionKey"] = session_key;
  }
  ForwardEventToJava(engine, "device.speech_interrupt", payload.dump().c_str());
}

void OnDeviceVibrate(const char* session_key,
                     int duration_ms,
                     void* user_data) {
  nlohmann::json payload = {{"durationMs", duration_ms}};
  if (session_key != nullptr && *session_key != '\0') {
    payload["sessionKey"] = session_key;
  }
  auto* engine = static_cast<EngineHandle*>(user_data);
  ForwardEventToJava(engine, "device.vibrate", payload.dump().c_str());
}

const char* OnDeviceWebSearch(const char* session_key,
                              const char* query,
                              int count,
                              const char* freshness,
                              void* user_data) {
  auto* engine = static_cast<EngineHandle*>(user_data);
  if (engine == nullptr || engine->vm == nullptr || engine->bridge_ref == nullptr) {
    return nullptr;
  }

  JNIEnv* env = nullptr;
  bool should_detach = false;
  const jint get_env_result =
      engine->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (get_env_result == JNI_EDETACHED) {
    if (engine->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
      return nullptr;
    }
    should_detach = true;
  } else if (get_env_result != JNI_OK || env == nullptr) {
    return nullptr;
  }

  thread_local std::string result_storage;
  result_storage.clear();

  jclass bridge_class = env->GetObjectClass(engine->bridge_ref);
  if (bridge_class == nullptr) {
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  jmethodID callback =
      env->GetMethodID(bridge_class, "onNativeWebSearch",
                       "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)Ljava/lang/String;");
  if (callback == nullptr) {
    env->DeleteLocalRef(bridge_class);
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  jstring j_session =
      env->NewStringUTF(session_key != nullptr ? session_key : "");
  jstring j_query = env->NewStringUTF(query != nullptr ? query : "");
  jstring j_freshness = env->NewStringUTF(freshness != nullptr ? freshness : "");
  auto* result = static_cast<jstring>(
      env->CallObjectMethod(engine->bridge_ref, callback, j_session, j_query,
                            count,
                            j_freshness));
  env->DeleteLocalRef(j_session);
  env->DeleteLocalRef(j_query);
  env->DeleteLocalRef(j_freshness);

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    env->DeleteLocalRef(bridge_class);
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  if (result != nullptr) {
    result_storage = ToString(env, result);
    env->DeleteLocalRef(result);
  }
  env->DeleteLocalRef(bridge_class);
  if (should_detach) {
    engine->vm->DetachCurrentThread();
  }
  return result_storage.empty() ? nullptr : result_storage.c_str();
}

const char* OnDeviceWebFetch(const char* session_key,
                             const char* url,
                             int max_chars,
                             void* user_data) {
  auto* engine = static_cast<EngineHandle*>(user_data);
  if (engine == nullptr || engine->vm == nullptr || engine->bridge_ref == nullptr) {
    return nullptr;
  }

  JNIEnv* env = nullptr;
  bool should_detach = false;
  const jint get_env_result =
      engine->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (get_env_result == JNI_EDETACHED) {
    if (engine->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
      return nullptr;
    }
    should_detach = true;
  } else if (get_env_result != JNI_OK || env == nullptr) {
    return nullptr;
  }

  thread_local std::string result_storage;
  result_storage.clear();

  jclass bridge_class = env->GetObjectClass(engine->bridge_ref);
  if (bridge_class == nullptr) {
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  jmethodID callback =
      env->GetMethodID(bridge_class, "onNativeWebFetch",
                       "(Ljava/lang/String;Ljava/lang/String;I)Ljava/lang/String;");
  if (callback == nullptr) {
    env->DeleteLocalRef(bridge_class);
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  jstring j_session =
      env->NewStringUTF(session_key != nullptr ? session_key : "");
  jstring j_url = env->NewStringUTF(url != nullptr ? url : "");
  auto* result = static_cast<jstring>(
      env->CallObjectMethod(engine->bridge_ref, callback, j_session, j_url,
                            max_chars));
  env->DeleteLocalRef(j_session);
  env->DeleteLocalRef(j_url);

  if (env->ExceptionCheck()) {
    env->ExceptionDescribe();
    env->ExceptionClear();
    env->DeleteLocalRef(bridge_class);
    if (should_detach) {
      engine->vm->DetachCurrentThread();
    }
    return nullptr;
  }

  if (result != nullptr) {
    result_storage = ToString(env, result);
    env->DeleteLocalRef(result);
  }
  env->DeleteLocalRef(bridge_class);
  if (should_detach) {
    engine->vm->DetachCurrentThread();
  }
  return result_storage.empty() ? nullptr : result_storage.c_str();
}

void ApplyDeviceCallbacks(EngineHandle* engine) {
  if (engine == nullptr || engine->engine == nullptr || engine->bridge_ref == nullptr) {
    return;
  }

  ravbot_mobile_device_callbacks_t callbacks{};
  callbacks.on_avatar_state = OnDeviceAvatarState;
  callbacks.on_speech_request = OnDeviceSpeechRequest;
  callbacks.on_speech_interrupt = OnDeviceSpeechInterrupt;
  callbacks.on_vibrate =
      engine->vibration_enabled ? OnDeviceVibrate : nullptr;
  callbacks.on_web_search =
      engine->web_search_enabled ? OnDeviceWebSearch : nullptr;
  callbacks.on_web_fetch =
      engine->web_fetch_enabled ? OnDeviceWebFetch : nullptr;
  ravbot_mobile_set_device_callbacks(engine->engine, &callbacks, engine);
}

void EnsureSubscribed(JNIEnv* env, jobject thiz, EngineHandle* engine) {
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  if (engine->bridge_ref != nullptr) {
    env->DeleteGlobalRef(engine->bridge_ref);
    engine->bridge_ref = nullptr;
  }
  engine->bridge_ref = env->NewGlobalRef(thiz);

  if (engine->subscription_id != 0) {
    ravbot_mobile_unsubscribe_events(engine->engine, engine->subscription_id);
    engine->subscription_id = 0;
  }
  engine->subscription_id =
      ravbot_mobile_subscribe_events(engine->engine, OnMobileEvent, engine);
  ApplyDeviceCallbacks(engine);
}

void ClearSubscription(JNIEnv* env, EngineHandle* engine) {
  if (engine == nullptr) {
    return;
  }

  if (engine->engine != nullptr && engine->subscription_id != 0) {
    ravbot_mobile_unsubscribe_events(engine->engine, engine->subscription_id);
    engine->subscription_id = 0;
  }
  if (env != nullptr && engine->bridge_ref != nullptr) {
    env->DeleteGlobalRef(engine->bridge_ref);
    engine->bridge_ref = nullptr;
  }
}

int64_t NowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeInitEngine(
    JNIEnv* env, jobject /* thiz */, jstring config_json, jstring state_dir,
    jstring models_dir) {
  auto* engine = new EngineHandle();
  env->GetJavaVM(&engine->vm);
  engine->state_dir = ToString(env, state_dir);
  engine->models_dir = ToString(env, models_dir);

  std::string config = ToString(env, config_json);
  engine->engine = ravbot_mobile_init_engine(config.c_str(),
                                             engine->state_dir.c_str(),
                                             engine->models_dir.c_str(),
                                             "info");
  if (engine->engine == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "Failed to create ravbot_mobile_core engine");
    delete engine;
    return 0;
  }

  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "Created ravbot_mobile_core engine with state dir: %s",
                      engine->state_dir.c_str());
  return reinterpret_cast<jlong>(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeFreeEngine(
    JNIEnv* env, jobject /* thiz */, jlong handle) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr) {
    return;
  }

  ClearSubscription(env, engine);
  if (engine->engine != nullptr) {
    ravbot_mobile_free_engine(engine->engine);
    engine->engine = nullptr;
  }
  delete engine;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeStartSession(
    JNIEnv* env, jobject /* thiz */, jlong handle, jstring session_id) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return JNI_FALSE;
  }

  engine->session_id = ToString(env, session_id);
  if (engine->session_id.empty()) {
    engine->session_id = kDefaultSessionId;
  }
  return ravbot_mobile_start_session(engine->engine, engine->session_id.c_str(),
                                     "RavBot Android")
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeSendTextTurn(
    JNIEnv* env, jobject /* thiz */, jlong handle, jstring text) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return JNI_FALSE;
  }

  const std::string prompt = ToString(env, text);
  return ravbot_mobile_send_text_turn(engine->engine, ActiveSessionId(engine),
                                      prompt.c_str())
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativePushPcm16(
    JNIEnv* env, jobject /* thiz */, jlong handle, jshortArray samples,
    jint sample_rate_hz, jint /* channels */, jboolean end_of_turn) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return JNI_FALSE;
  }

  std::vector<int16_t> pcm = ToShortVector(env, samples);
  if (pcm.empty()) {
    return JNI_FALSE;
  }

  return ravbot_mobile_push_pcm16(engine->engine, ActiveSessionId(engine),
                                  pcm.data(), pcm.size(), sample_rate_hz,
                                  end_of_turn == JNI_TRUE)
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeFlushAudioTurn(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return JNI_FALSE;
  }

  return ravbot_mobile_flush_audio_turn(engine->engine, ActiveSessionId(engine))
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativePushCameraFrame(
    JNIEnv* env, jobject /* thiz */, jlong handle, jint width, jint height,
    jint /* stride */, jstring format, jlong timestamp_ms, jbyteArray pixels) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return JNI_FALSE;
  }

  std::vector<uint8_t> frame = ToByteVector(env, pixels);
  if (frame.empty()) {
    return JNI_FALSE;
  }

  const std::string frame_format = ToString(env, format);
  const int64_t frame_timestamp_ms =
      timestamp_ms > 0 ? static_cast<int64_t>(timestamp_ms) : NowMillis();
  return ravbot_mobile_push_camera_frame(
             engine->engine, ActiveSessionId(engine), frame.data(),
             frame.size(), width, height,
             frame_format.empty() ? "rgba8888" : frame_format.c_str(),
             frame_timestamp_ms)
             ? JNI_TRUE
             : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeInterruptGeneration(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  ravbot_mobile_interrupt_generation(engine->engine, ActiveSessionId(engine));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeReportTtsState(
    JNIEnv* env, jobject /* thiz */, jlong handle, jstring state) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  const std::string playback_state = ToString(env, state);
  if (playback_state.empty()) {
    return;
  }

  ravbot_mobile_report_tts_state(engine->engine, ActiveSessionId(engine),
                                 playback_state.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeReportDeviceStatus(
    JNIEnv* env, jobject /* thiz */, jlong handle, jboolean service_running,
    jboolean capture_requested, jboolean permissions_granted,
    jboolean host_web_search_enabled, jboolean host_web_fetch_enabled,
    jboolean host_haptics_enabled, jstring microphone_status,
    jstring camera_status, jstring speaker_status) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  const std::string microphone = ToString(env, microphone_status);
  const std::string camera = ToString(env, camera_status);
  const std::string speaker = ToString(env, speaker_status);

  ravbot_mobile_report_device_status(
      engine->engine, ActiveSessionId(engine), service_running == JNI_TRUE,
      capture_requested == JNI_TRUE, permissions_granted == JNI_TRUE,
      host_web_search_enabled == JNI_TRUE, host_web_fetch_enabled == JNI_TRUE,
      host_haptics_enabled == JNI_TRUE,
      microphone.empty() ? "stopped" : microphone.c_str(),
      camera.empty() ? "stopped" : camera.c_str(),
      speaker.empty() ? "idle" : speaker.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeSetForegroundState(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle, jboolean is_foreground) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  ravbot_mobile_set_foreground_state(engine->engine,
                                     is_foreground == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeSubscribeEvents(
    JNIEnv* env, jobject thiz, jlong handle) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr || engine->engine == nullptr) {
    return;
  }

  EnsureSubscribed(env, thiz, engine);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeUnsubscribeEvents(
    JNIEnv* env, jobject /* thiz */, jlong handle) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr) {
    return;
  }

  ClearSubscription(env, engine);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeSetHostWebCapabilities(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle,
    jboolean web_search_enabled, jboolean web_fetch_enabled) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr) {
    return;
  }

  engine->web_search_enabled = web_search_enabled == JNI_TRUE;
  engine->web_fetch_enabled = web_fetch_enabled == JNI_TRUE;
  ApplyDeviceCallbacks(engine);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ravbot_android_bridge_RavbotNativeBridge_nativeSetHapticsCapability(
    JNIEnv* /* env */, jobject /* thiz */, jlong handle,
    jboolean vibration_enabled) {
  auto* engine = FromHandle(handle);
  if (engine == nullptr) {
    return;
  }

  engine->vibration_enabled = vibration_enabled == JNI_TRUE;
  ApplyDeviceCallbacks(engine);
}
