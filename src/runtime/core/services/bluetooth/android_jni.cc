#if defined(__ANDROID__)
#include <cstdint>
#include <vector>

#include "../../../platform/android.hh"
#include "../../../app.hh"
#include "../../../debug.hh"
#include "../../../string.hh"
#include "../../../runtime.hh"
#include "../../services.hh"

extern "C" void oro_bluetooth_android_onDeviceFound(
  const char* id,
  const char* name,
  int rssi,
  const char** services,
  size_t serviceCount,
  const uint16_t* manufacturerIds,
  const unsigned char* const* manufacturerData,
  const size_t* manufacturerLengths,
  size_t manufacturerCount
);

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onDeviceFound) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jName,
  jint jRssi,
  jobjectArray jServices,
  jintArray jManufacturerIds,
  jobjectArray jManufacturerData
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* name = env->GetStringUTFChars(jName, nullptr);

  std::vector<std::string> servicesTmp;
  std::vector<const char*> servicePtrs;
  if (jServices) {
    const jsize len = env->GetArrayLength(jServices);
    servicesTmp.reserve((size_t) len);
    for (jsize i = 0; i < len; ++i) {
      jstring js = (jstring) env->GetObjectArrayElement(jServices, i);
      const char* svc = js ? env->GetStringUTFChars(js, nullptr) : nullptr;
      servicesTmp.emplace_back(svc ? svc : "");
      if (js && svc) env->ReleaseStringUTFChars(js, svc);
      if (js) env->DeleteLocalRef(js);
    }
    servicePtrs.reserve(servicesTmp.size());
    for (const auto& s : servicesTmp) {
      servicePtrs.push_back(s.c_str());
    }
  }

  std::vector<uint16_t> manufIds;
  std::vector<std::vector<unsigned char>> manufBuffers;
  std::vector<const unsigned char*> manufDataPtrs;
  std::vector<size_t> manufLengths;

  if (jManufacturerIds && jManufacturerData) {
    const jsize idLen = env->GetArrayLength(jManufacturerIds);
    const jsize dataLen = env->GetArrayLength(jManufacturerData);
    if (idLen == dataLen) {
      std::vector<jint> intIds((size_t) idLen);
      env->GetIntArrayRegion(jManufacturerIds, 0, idLen, intIds.data());
      manufIds.reserve(idLen);
      manufBuffers.reserve(idLen);
      for (jsize i = 0; i < idLen; ++i) {
        auto jBytes = (jbyteArray) env->GetObjectArrayElement(jManufacturerData, i);
        std::vector<unsigned char> buffer;
        if (jBytes) {
          const jsize len = env->GetArrayLength(jBytes);
          buffer.resize((size_t) len);
          if (len > 0) {
            env->GetByteArrayRegion(jBytes, 0, len, reinterpret_cast<jbyte*>(buffer.data()));
          }
          env->DeleteLocalRef(jBytes);
        }
        manufBuffers.push_back(std::move(buffer));
        const jint rawId = intIds[(size_t) i];
        uint16_t companyId = 0;
        if (rawId >= 0 && rawId <= 0xFFFF) {
          companyId = static_cast<uint16_t>(rawId);
        } else {
          companyId = static_cast<uint16_t>(rawId & 0xFFFF);
        }
        manufIds.push_back(companyId);
      }
      manufLengths.resize(manufBuffers.size());
      manufDataPtrs.resize(manufBuffers.size());
      for (size_t i = 0; i < manufBuffers.size(); ++i) {
        manufDataPtrs[i] = manufBuffers[i].empty() ? nullptr : manufBuffers[i].data();
        manufLengths[i] = manufBuffers[i].size();
      }
    }
  }

  oro_bluetooth_android_onDeviceFound(
    id ? id : "",
    name ? name : "",
    (int) jRssi,
    servicePtrs.empty() ? nullptr : servicePtrs.data(),
    servicePtrs.size(),
    manufIds.empty() ? nullptr : manufIds.data(),
    manufBuffers.empty() ? nullptr : manufDataPtrs.data(),
    manufBuffers.empty() ? nullptr : manufLengths.data(),
    manufIds.size()
  );

  env->ReleaseStringUTFChars(jId, id);
  env->ReleaseStringUTFChars(jName, name);
}

extern "C" void oro_bluetooth_android_onGattConnected(const char* id, int ok, const char* message);
extern "C" void oro_bluetooth_android_onGattDisconnected(const char* id, const char* reason);
extern "C" void oro_bluetooth_android_onServicesDiscovered(const char* id, const char** services, size_t n);
extern "C" void oro_bluetooth_android_onCharacteristicsDiscovered(const char* id, const char* service, const char** chars, size_t n);
extern "C" void oro_bluetooth_android_onCharacteristicRead(const char* id, const char* service, const char* characteristic, const unsigned char* data, size_t n);
extern "C" void oro_bluetooth_android_onCharacteristicWrite(const char* id, const char* service, const char* characteristic, int ok, const char* message);
extern "C" void oro_bluetooth_android_onCharacteristicChanged(const char* id, const char* service, const char* characteristic, const unsigned char* data, size_t n);

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onGattConnected) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jboolean jOk,
  jstring jMessage
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* msg = jMessage ? env->GetStringUTFChars(jMessage, nullptr) : nullptr;
  oro_bluetooth_android_onGattConnected(id ? id : "", jOk ? 1 : 0, msg ? msg : "");
  env->ReleaseStringUTFChars(jId, id);
  if (jMessage) env->ReleaseStringUTFChars(jMessage, msg);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onGattDisconnected) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jReason
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* reason = jReason ? env->GetStringUTFChars(jReason, nullptr) : nullptr;
  oro_bluetooth_android_onGattDisconnected(id ? id : "", reason ? reason : "");
  env->ReleaseStringUTFChars(jId, id);
  if (jReason) env->ReleaseStringUTFChars(jReason, reason);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onServicesDiscovered) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jobjectArray jServices
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  size_t n = jServices ? (size_t) env->GetArrayLength(jServices) : 0;
  std::vector<const char*> ptrs; ptrs.reserve(n);
  std::vector<std::string> tmp; tmp.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    jstring js = (jstring) env->GetObjectArrayElement(jServices, (jsize) i);
    const char* s = js ? env->GetStringUTFChars(js, nullptr) : "";
    tmp.emplace_back(s ? s : "");
    if (js) env->ReleaseStringUTFChars(js, s);
    if (js) env->DeleteLocalRef(js);
  }
  for (auto &s : tmp) ptrs.push_back(s.c_str());
  oro_bluetooth_android_onServicesDiscovered(id ? id : "", ptrs.data(), ptrs.size());
  env->ReleaseStringUTFChars(jId, id);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onCharacteristicsDiscovered) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jService,
  jobjectArray jChars
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* service = env->GetStringUTFChars(jService, nullptr);
  size_t n = jChars ? (size_t) env->GetArrayLength(jChars) : 0;
  std::vector<const char*> ptrs; ptrs.reserve(n);
  std::vector<std::string> tmp; tmp.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    jstring js = (jstring) env->GetObjectArrayElement(jChars, (jsize) i);
    const char* s = js ? env->GetStringUTFChars(js, nullptr) : "";
    tmp.emplace_back(s ? s : "");
    if (js) env->ReleaseStringUTFChars(js, s);
    if (js) env->DeleteLocalRef(js);
  }
  for (auto &s : tmp) ptrs.push_back(s.c_str());
  oro_bluetooth_android_onCharacteristicsDiscovered(id ? id : "", service ? service : "", ptrs.data(), ptrs.size());
  env->ReleaseStringUTFChars(jId, id);
  env->ReleaseStringUTFChars(jService, service);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onCharacteristicRead) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jService,
  jstring jChar,
  jbyteArray jValue
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* service = env->GetStringUTFChars(jService, nullptr);
  const char* chr = env->GetStringUTFChars(jChar, nullptr);
  unsigned char* data = nullptr;
  size_t n = 0;
  if (jValue) {
    n = (size_t) env->GetArrayLength(jValue);
    data = (unsigned char*) env->GetPrimitiveArrayCritical(jValue, nullptr);
  }
  oro_bluetooth_android_onCharacteristicRead(id ? id : "", service ? service : "", chr ? chr : "", data, n);
  if (jValue) env->ReleasePrimitiveArrayCritical(jValue, data, JNI_ABORT);
  env->ReleaseStringUTFChars(jId, id);
  env->ReleaseStringUTFChars(jService, service);
  env->ReleaseStringUTFChars(jChar, chr);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onCharacteristicWrite) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jService,
  jstring jChar,
  jboolean jOk,
  jstring jMessage
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* service = env->GetStringUTFChars(jService, nullptr);
  const char* chr = env->GetStringUTFChars(jChar, nullptr);
  const char* msg = jMessage ? env->GetStringUTFChars(jMessage, nullptr) : nullptr;
  oro_bluetooth_android_onCharacteristicWrite(id ? id : "", service ? service : "", chr ? chr : "", jOk ? 1 : 0, msg ? msg : "");
  env->ReleaseStringUTFChars(jId, id);
  env->ReleaseStringUTFChars(jService, service);
  env->ReleaseStringUTFChars(jChar, chr);
  if (jMessage) env->ReleaseStringUTFChars(jMessage, msg);
}

extern "C" void ANDROID_EXTERNAL(bluetooth, BLEManager, onCharacteristicChanged) (
  JNIEnv* env,
  jclass,
  jstring jId,
  jstring jService,
  jstring jChar,
  jbyteArray jValue
) {
  const char* id = env->GetStringUTFChars(jId, nullptr);
  const char* service = env->GetStringUTFChars(jService, nullptr);
  const char* chr = env->GetStringUTFChars(jChar, nullptr);
  unsigned char* data = nullptr; size_t n = 0;
  if (jValue) {
    n = (size_t) env->GetArrayLength(jValue);
    data = (unsigned char*) env->GetPrimitiveArrayCritical(jValue, nullptr);
  }
  oro_bluetooth_android_onCharacteristicChanged(id ? id : "", service ? service : "", chr ? chr : "", data, n);
  if (jValue) env->ReleasePrimitiveArrayCritical(jValue, data, JNI_ABORT);
  env->ReleaseStringUTFChars(jId, id);
  env->ReleaseStringUTFChars(jService, service);
  env->ReleaseStringUTFChars(jChar, chr);
}
#endif
