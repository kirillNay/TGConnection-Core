#include "tg-connection-core/connection/ExternalLogger.h"

#include <jni.h>
#include <memory>
#include <string>

class JniExternalLogger final : public ExternalLogger {
public:
    JniExternalLogger(JavaVM* jvm, JNIEnv* env, jobject nativeLoggerObj)
        : jvm_(jvm) {
        obj_ = env->NewGlobalRef(nativeLoggerObj);
        jclass cls = env->GetObjectClass(nativeLoggerObj);
        logMethod_ = env->GetMethodID(cls, "log", "(ILjava/lang/String;)V");
    }

    ~JniExternalLogger() override {
        JNIEnv* env = nullptr;
        bool didAttach = false;
        if (ensureEnv(&env, &didAttach) && env && obj_) {
            env->DeleteGlobalRef(obj_);
            obj_ = nullptr;
        }
        if (didAttach) {
            jvm_->DetachCurrentThread();
        }
    }

    void log(int level, const std::string& message) override {
        JNIEnv* env = nullptr;
        bool didAttach = false;
        if (!ensureEnv(&env, &didAttach) || !env) return;

        jstring jmsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(obj_, logMethod_, (jint)level, jmsg);
        env->DeleteLocalRef(jmsg);

        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }

        if (didAttach) {
            jvm_->DetachCurrentThread();
        }
    }

private:
    JavaVM* jvm_;
    jobject obj_ = nullptr;
    jmethodID logMethod_ = nullptr;

    bool ensureEnv(JNIEnv** env, bool* didAttach) const {
        *didAttach = false;
        jint getEnvRes = jvm_->GetEnv((void**)env, JNI_VERSION_1_6);
        if (getEnvRes == JNI_OK) return true;

        if (getEnvRes == JNI_EDETACHED) {
            if (jvm_->AttachCurrentThread(env, nullptr) == JNI_OK) {
                *didAttach = true;
                return true;
            }
        }
        return false;
    }
};
