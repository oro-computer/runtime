#include <jni.h>
#include <cassert>
#include <string>
#include <thread>

#include "secure-storage-class-loader.hh"

int main (int argc, char** argv) {
  assert(argc == 4);
  JavaVM* vm = nullptr;
  JNIEnv* env = nullptr;
  std::string classpath = std::string("-Djava.class.path=") + argv[1];
  JavaVMOption options[2] = {{classpath.data(), nullptr}, {const_cast<char*>("-Xcheck:jni"), nullptr}};
  JavaVMInitArgs args{};
  args.version = JNI_VERSION_1_8;
  args.nOptions = 2;
  args.options = options;
  assert(JNI_CreateJavaVM(&vm, reinterpret_cast<void**>(&env), &args) == JNI_OK);
  auto contextClass = env->FindClass("ProbeContext");
  assert(contextClass && !env->ExceptionCheck());
  auto ctor = env->GetMethodID(contextClass, "<init>", "(Ljava/lang/String;)V");
  assert(ctor && !env->ExceptionCheck());
  for (int index = 2; index <= 3; ++index) {
    auto path = env->NewStringUTF(argv[index]);
    auto local = env->NewObject(contextClass, ctor, path);
    assert(local && !env->ExceptionCheck());
    auto context = env->NewGlobalRef(local);
    std::thread worker([&] {
      JNIEnv* workerEnv = nullptr;
      assert(vm->AttachCurrentThread(reinterpret_cast<void**>(&workerEnv), nullptr) == JNI_OK);
      assert(workerEnv->FindClass("oro/runtime/securestorage/SecureStorageManager") == nullptr);
      assert(workerEnv->ExceptionCheck());
      workerEnv->ExceptionClear();
      auto cls = loadSecureStorageClass(workerEnv, context);
      assert(!workerEnv->ExceptionCheck());
      if (index == 2) {
        assert(cls);
        auto method = workerEnv->GetStaticMethodID(cls, "probe", "()Z");
        assert(method && !workerEnv->ExceptionCheck());
        assert(workerEnv->CallStaticBooleanMethod(cls, method) == JNI_TRUE);
        assert(!workerEnv->ExceptionCheck());
        workerEnv->DeleteLocalRef(cls);
      } else {
        assert(cls == nullptr);
      }
      auto nextClass = workerEnv->GetObjectClass(context);
      assert(nextClass && !workerEnv->ExceptionCheck());
      workerEnv->DeleteLocalRef(nextClass);
      assert(vm->DetachCurrentThread() == JNI_OK);
    });
    worker.join();
    env->DeleteGlobalRef(context);
    env->DeleteLocalRef(local);
    env->DeleteLocalRef(path);
  }
  env->DeleteLocalRef(contextClass);
  assert(vm->DestroyJavaVM() == JNI_OK);
}
