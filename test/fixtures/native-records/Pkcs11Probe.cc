// Public test adapter over a real TPM2 PKCS#11 module. This is not hardware attestation.
#include <pkcs11/pkcs11.h>
#include <dlfcn.h>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cstdio>

namespace {
  CK_FUNCTION_LIST_PTR actual = nullptr;
  CK_FUNCTION_LIST proxy;
  std::atomic<unsigned int> mode = 0;
  std::atomic<unsigned int> userLogins = 0;
  std::atomic<unsigned int> operationLogins = 0;
  CK_RV information (CK_INFO_PTR info) {
    const auto result = actual->C_GetInfo(info);
    if (result == CKR_OK) {
      memset(info->libraryDescription, ' ', sizeof(info->libraryDescription));
      const auto description = getenv("ORO_PUBLIC_PROVIDER_FAMILY");
      const char* label =
        description && strcmp(description, "pkcs11") == 0 ? "Public PKCS11 test adapter" : "Public TPM2 test adapter";
      memcpy(info->libraryDescription, label, strlen(label));
    }
    return result;
  }
  CK_RV slot (CK_SLOT_ID id, CK_SLOT_INFO_PTR info) {
    const auto result = actual->C_GetSlotInfo(id, info);
    if (result == CKR_OK && (mode & 32))
      info->flags &= ~CKF_HW_SLOT;
    return result;
  }
  CK_RV token (CK_SLOT_ID id, CK_TOKEN_INFO_PTR info) {
    const auto result = actual->C_GetTokenInfo(id, info);
    if (result == CKR_OK && (mode & 64))
      info->serialNumber[0] ^= 1;
    return result;
  }
  CK_RV login (CK_SESSION_HANDLE session, CK_USER_TYPE kind, CK_UTF8CHAR_PTR pin, CK_ULONG size) {
    if (kind == CKU_USER)
      userLogins++;
    if (kind == CKU_CONTEXT_SPECIFIC)
      operationLogins++;
    const auto result = actual->C_Login(session, kind, pin, size);
    if (result != CKR_OK)
      fprintf(stderr, "public-provider-login-status:%lu kind:%lu\n", result, kind);
    return result;
  }
  CK_RV attributes (CK_SESSION_HANDLE session, CK_OBJECT_HANDLE object, CK_ATTRIBUTE_PTR values, CK_ULONG count) {
    const auto result = actual->C_GetAttributeValue(session, object, values, count);
    if (result != CKR_OK)
      fprintf(stderr, "public-provider-attribute-status:%lu attribute:%lu\n", result, count ? values[0].type : 0);
    if (result == CKR_OK && (mode & 1)) {
      for (CK_ULONG i = 0; i < count; i++) {
        if (values[i].type == CKA_ALWAYS_AUTHENTICATE && values[i].pValue && values[i].ulValueLen == sizeof(CK_BBOOL))
          *static_cast<CK_BBOOL*>(values[i].pValue) = CK_FALSE;
      }
    }
    return result;
  }
  CK_RV sign (CK_SESSION_HANDLE session, CK_BYTE_PTR input, CK_ULONG size, CK_BYTE_PTR output, CK_ULONG_PTR length) {
    if (mode & 8)
      std::this_thread::sleep_for(std::chrono::milliseconds(750));
    const auto result = actual->C_Sign(session, input, size, output, length);
    if (result == CKR_OK && (mode & 2) && output && *length)
      output[0] ^= 1;
    return result;
  }
  CK_RV generate (CK_SESSION_HANDLE session, CK_MECHANISM_PTR mechanism, CK_ATTRIBUTE_PTR pub, CK_ULONG pubs,
                  CK_ATTRIBUTE_PTR priv, CK_ULONG privs, CK_OBJECT_HANDLE_PTR publicKey,
                  CK_OBJECT_HANDLE_PTR privateKey) {
    if (mode & 4)
      return CKR_DEVICE_ERROR;
    const auto result = actual->C_GenerateKeyPair(session, mechanism, pub, pubs, priv, privs, publicKey, privateKey);
    if (result != CKR_OK)
      fprintf(stderr, "public-provider-generate-status:%lu\n", result);
    return result;
  }
  CK_RV encrypt (CK_SESSION_HANDLE session, CK_BYTE_PTR input, CK_ULONG size, CK_BYTE_PTR output, CK_ULONG_PTR length) {
    const auto result = actual->C_Encrypt(session, input, size, output, length);
    fprintf(stderr, "public-provider-wrap-status:%lu bytes:%lu\n", result, *length);
    return result;
  }
  CK_RV decrypt (CK_SESSION_HANDLE session, CK_BYTE_PTR input, CK_ULONG size, CK_BYTE_PTR output, CK_ULONG_PTR length) {
    const auto result = actual->C_Decrypt(session, input, size, output, length);
    fprintf(stderr, "public-provider-unwrap-status:%lu bytes:%lu\n", result, *length);
    return result;
  }
  CK_RV destroy (CK_SESSION_HANDLE session, CK_OBJECT_HANDLE object) {
    if (mode & 16)
      return CKR_DEVICE_ERROR;
    return actual->C_DestroyObject(session, object);
  }
}  // namespace
extern "C" {
void oro_public_pkcs11_mode (unsigned int value) {
  mode = value;
}
unsigned int oro_public_pkcs11_logins (bool context) {
  return context ? operationLogins.load() : userLogins.load();
}
CK_RV C_GetFunctionList (CK_FUNCTION_LIST_PTR_PTR output) {
  if (!actual) {
    const auto library =
      dlopen("/usr/lib/x86_64-linux-gnu/pkcs11/libtpm2_pkcs11.so", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library)
      return CKR_DEVICE_ERROR;
    const auto get = reinterpret_cast<CK_C_GetFunctionList>(dlsym(library, "C_GetFunctionList"));
    if (!get || get(&actual) != CKR_OK)
      return CKR_DEVICE_ERROR;
    proxy = *actual;
    proxy.C_GetInfo = information;
    proxy.C_GetSlotInfo = slot;
    proxy.C_GetTokenInfo = token;
    proxy.C_Login = login;
    proxy.C_GetAttributeValue = attributes;
    proxy.C_Sign = sign;
    proxy.C_GenerateKeyPair = generate;
    proxy.C_DestroyObject = destroy;
    proxy.C_Encrypt = encrypt;
    proxy.C_Decrypt = decrypt;
  }
  *output = &proxy;
  return CKR_OK;
}
}
