#include "string_wrap.hh"
#include <cstdint>
#include <limits>
#include <utility>

namespace oro::runtime::android {
  StringWrap::StringWrap (JNIEnv *env) {
    this->env = env;
    this->ref = nullptr;
    this->length = 0;
    this->string = nullptr;
    this->needsRelease = false;
  }

  StringWrap::StringWrap (const StringWrap& copy) : StringWrap(copy.env) {
    this->set(copy.ref);
  }

  StringWrap::StringWrap (StringWrap&& copy)
    : env(copy.env), ref(copy.ref), string(copy.string), length(copy.length), needsRelease(copy.needsRelease) {
    copy.ref = nullptr;
    copy.string = nullptr;
    copy.length = 0;
    copy.needsRelease = false;
  }

  StringWrap::StringWrap (JNIEnv *env, jstring ref) : StringWrap(env) {
    if (ref) {
      this->set(ref);
    }
  }

  StringWrap::StringWrap (JNIEnv *env, jobject ref)
    : StringWrap(env, (jstring) ref)
  {}

  StringWrap::StringWrap (JNIEnv *env, String string) : StringWrap(env) {
    this->set(std::move(string));
  }

  StringWrap::StringWrap (JNIEnv *env, const char *string) : StringWrap(env) {
    if (string) {
      this->set(string);
    }
  }

  StringWrap::~StringWrap () {
    this->release();
  }

  const StringWrap& StringWrap::operator= (const StringWrap& other) {
    if (this == &other) return *this;
    this->release();
    this->env = other.env;
    this->set(other.ref);
    return *this;
  }

  StringWrap& StringWrap::operator= (StringWrap&& other) {
    if (this == &other) return *this;
    this->release();
    this->env = other.env;
    this->ref = other.ref;
    this->string = other.string;
    this->length = other.length;
    this->needsRelease = other.needsRelease;
    other.ref = nullptr;
    other.string = nullptr;
    other.length = 0;
    other.needsRelease = false;
    return *this;
  }

  void StringWrap::set (String value) {
    if (!this->env || this->env->ExceptionCheck()) {
      this->release();
      return;
    }
    Vector<jchar> units;
    units.reserve(value.size());
    for (size_t index = 0; index < value.size();) {
      const auto first = static_cast<uint8_t>(value[index]);
      uint32_t code = first;
      size_t count = 1;
      uint32_t minimum = 0;
      if (first >= 0xc2 && first <= 0xdf) {
        code = first & 0x1f;
        count = 2;
        minimum = 0x80;
      } else if (first >= 0xe0 && first <= 0xef) {
        code = first & 0x0f;
        count = 3;
        minimum = 0x800;
      } else if (first >= 0xf0 && first <= 0xf4) {
        code = first & 0x07;
        count = 4;
        minimum = 0x10000;
      } else if (first >= 0x80) {
        code = 0xfffd;
      }
      bool valid = index + count <= value.size();
      if (valid) {
        for (size_t next = 1; next < count; ++next) {
          const auto byte = static_cast<uint8_t>(value[index + next]);
          if ((byte & 0xc0) != 0x80) {
            valid = false;
            break;
          }
          code = (code << 6) | (byte & 0x3f);
        }
      }
      if (!valid || code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
        code = 0xfffd;
        count = 1;
      }
      index += count;
      if (code > 0xffff) {
        code -= 0x10000;
        units.push_back(static_cast<jchar>(0xd800 + (code >> 10)));
        units.push_back(static_cast<jchar>(0xdc00 + (code & 0x3ff)));
      } else {
        units.push_back(static_cast<jchar>(code));
      }
    }
    this->release();
    if (!this->env || units.size() > static_cast<size_t>(std::numeric_limits<jsize>::max())) return;
    const jchar empty = 0;
    this->set(this->env->NewString(units.empty() ? &empty : units.data(), static_cast<jsize>(units.size())));
  }

  void StringWrap::set (const char* value) {
    if (value) this->set(String(value));
    else this->release();
  }

  void StringWrap::set (jstring value) {
    this->release();
    if (value && this->env && !this->env->ExceptionCheck()) {
      this->ref = value;
      this->string = this->env->GetStringUTFChars(value, nullptr);
      // GetStringUTFChars must be released even when the VM reports no copy.
      this->needsRelease = this->string != nullptr;
      if (this->string) this->length = this->env->GetStringUTFLength(value);
    }
  }

  void StringWrap::release () {
    if (this->ref && this->string && this->needsRelease) {
      this->env->ReleaseStringUTFChars(this->ref, this->string);
    }

    this->ref = nullptr;
    this->length = 0;
    this->string = nullptr;
    this->needsRelease = false;
  }

  const char* StringWrap::c_str () {
    return this->string;
  }

  const String StringWrap::str () {
    String value;
    if (!this->env || !this->ref || !this->string || this->env->ExceptionCheck()) return value;
    const auto units = this->env->GetStringChars(this->ref, nullptr);
    if (!units) return value;
    const auto length = this->env->GetStringLength(this->ref);
    for (jsize index = 0; index < length; ++index) {
      uint32_t code = units[index];
      if (code >= 0xd800 && code <= 0xdbff && index + 1 < length && units[index + 1] >= 0xdc00 && units[index + 1] <= 0xdfff) {
        code = 0x10000 + ((code - 0xd800) << 10) + (units[++index] - 0xdc00);
      } else if (code >= 0xd800 && code <= 0xdfff) {
        code = 0xfffd;
      }
      if (code < 0x80) {
        value.push_back(static_cast<char>(code));
      } else if (code < 0x800) {
        value.push_back(static_cast<char>(0xc0 | (code >> 6)));
        value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
      } else if (code < 0x10000) {
        value.push_back(static_cast<char>(0xe0 | (code >> 12)));
        value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
      } else {
        value.push_back(static_cast<char>(0xf0 | (code >> 18)));
        value.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        value.push_back(static_cast<char>(0x80 | (code & 0x3f)));
      }
    }
    this->env->ReleaseStringChars(this->ref, units);
    return value;
  }

  const jstring StringWrap::j_str () {
    return this->ref;
  }

  const size_t StringWrap::size () {
    if (!this->string || !this->ref) {
      return 0;
    }

    return this->length;
  }
}
