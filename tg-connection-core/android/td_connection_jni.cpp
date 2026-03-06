#include <td/telegram/Client.h>
#include <td/telegram/td_api.h>

#include <td/tl/tl_jni_object.h>

#include "tg-connection-core/connection/TdConnection.h"
#include "tg-connection-core/connection/Record.h"
#include "tg-connection-core/connection/TestProxyResult.h"
#include "JniExternalLogger.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <jni.h>
#include <string>
#include <vector>
#include <unordered_map>

// =====================================================
// Helpers
// =====================================================

static TdConnection* ptr(jlong p) {
    return reinterpret_cast<TdConnection*>(p);
}

static jstring jstr(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// =====================================================
// Record -> Java DTO
// Java ctor:
// Record(String ip, int attempts, int success,
//        double successRate, double p50, double p95,
//        double jitter, double min, double max)
// Signature: (Ljava/lang/String;IIDDDDDD)V
// =====================================================

static jobject convertRecord(JNIEnv* env, const Record& r) {
    std::string ip = r.ip_address().get_ip_str().str();

    jclass cls = env->FindClass("nay/kirill/tdconnection/native/Record");
    jmethodID mid = env->GetStaticMethodID(
        cls,
        "create",
        "(Ljava/lang/String;IIDDDDDD)Lnay/kirill/tdconnection/native/Record;"
    );

    return env->CallStaticObjectMethod(
        cls,
        mid,
        jstr(env, ip),
        (jint) r.attempts(),
        (jint) r.success(),
        (jdouble) r.success_rate(),
        (jdouble) r.p50(),
        (jdouble) r.p95(),
        (jdouble) r.jitter(),
        (jdouble) r.min(),
        (jdouble) r.max()
    );
}

static jobject convertRecords(
    JNIEnv* env,
    const std::vector<Record>& records
) {
    jclass listCls = env->FindClass("java/util/ArrayList");
    jmethodID ctor = env->GetMethodID(listCls, "<init>", "()V");
    jmethodID add = env->GetMethodID(
        listCls,
        "add",
        "(Ljava/lang/Object;)Z"
    );

    jobject list = env->NewObject(listCls, ctor);

    for (const auto& r : records) {
        jobject jr = convertRecord(env, r);
        env->CallBooleanMethod(list, add, jr);
        env->DeleteLocalRef(jr);
    }

    return list;
}

// =====================================================
// TestProxyResult -> Java DTO
// Java ctor:
// TestProxyResult(double p50, double p95,
//                 double jitter, double successRate)
// Signature: (DDDD)V
// =====================================================

static jobject convertTestProxyResult(
    JNIEnv* env,
    const TestProxyResult& r
) {
    jclass cls =
        env->FindClass("nay/kirill/tdconnection/native/TestProxyResult");

    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(DDDD)V"
    );

    return env->NewObject(
        cls,
        ctor,
        r.p50(),
        r.p95(),
        r.jitter(),
        r.success_rate()
    );
}

// =====================================================
// Map<int, uint64_t> -> HashMap<Integer, Long>
// =====================================================

static jobject convertCounters(
    JNIEnv* env,
    const std::unordered_map<std::int32_t, std::uint64_t>& map
) {
    jclass mapCls = env->FindClass("java/util/HashMap");
    jmethodID ctor = env->GetMethodID(mapCls, "<init>", "()V");
    jmethodID put = env->GetMethodID(
        mapCls,
        "put",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;"
    );

    jclass intCls = env->FindClass("java/lang/Integer");
    jmethodID intCtor = env->GetMethodID(intCls, "<init>", "(I)V");

    jclass longCls = env->FindClass("java/lang/Long");
    jmethodID longCtor = env->GetMethodID(longCls, "<init>", "(J)V");

    jobject result = env->NewObject(mapCls, ctor);

    for (auto& [k, v] : map) {
        jobject jk = env->NewObject(intCls, intCtor, k);
        jobject jv = env->NewObject(longCls, longCtor, (jlong)v);
        env->CallObjectMethod(result, put, jk, jv);
        env->DeleteLocalRef(jk);
        env->DeleteLocalRef(jv);
    }

    return result;
}

// =====================================================
// JNI API
// =====================================================

extern "C" JNIEXPORT jlong JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeCreate(
    JNIEnv* env, jobject, jobject nativeLogger, jint apiId, jstring apiHash
) {
    JavaVM* jvm = nullptr;
    env->GetJavaVM(&jvm);

    auto logger = std::make_unique<JniExternalLogger>(jvm, env, nativeLogger);
    const char* api_hash_chars = apiHash ? env->GetStringUTFChars(apiHash, nullptr) : nullptr;

    TdConnection::TdlibCredentials credentials;
    credentials.api_id = static_cast<std::int32_t>(apiId);
    credentials.api_hash = api_hash_chars ? api_hash_chars : "";

    if (api_hash_chars) {
        env->ReleaseStringUTFChars(apiHash, api_hash_chars);
    }

    auto* conn = new TdConnection(std::move(logger), std::move(credentials));
    return reinterpret_cast<jlong>(conn);
}

extern "C" JNIEXPORT void JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeStart(
    JNIEnv* env, jobject,
    jlong p,
    jstring databaseDir,
    jstring filesDir,
    jstring caPath
) {
    const char* db = env->GetStringUTFChars(databaseDir, nullptr);
    const char* files = env->GetStringUTFChars(filesDir, nullptr);
    const char* ca_path = env->GetStringUTFChars(caPath, nullptr);

    ptr(p)->start(db, files, ca_path);

    env->ReleaseStringUTFChars(databaseDir, db);
    env->ReleaseStringUTFChars(filesDir, files);
    env->ReleaseStringUTFChars(caPath, ca_path);
}

extern "C" JNIEXPORT void JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeFetchIps(
    JNIEnv*, jobject, jlong p, jint size
) {
    ptr(p)->fetch_ips(size);
}

extern "C" JNIEXPORT jobject JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeCheckConnection(
    JNIEnv* env, jobject, jlong p, jint timeoutMs
) {
    ConnectionTestResult r =
        ptr(p)->check_connection(timeoutMs);

    jclass cls =
        env->FindClass("nay/kirill/tdconnection/native/ConnectionTestResult");

    jmethodID ctor = env->GetMethodID(
        cls,
        "<init>",
        "(ZLjava/lang/String;DDDDLjava/util/List;"
        "Lnay/kirill/tdconnection/native/TestProxyResult;"
        "Ljava/util/Map;)V"
    );

    jobject records = convertRecords(env, r.tcp_records);
    jobject proxy   = convertTestProxyResult(env, r.test_proxy_res);
    jobject counters = convertCounters(env, r.state_counters);

    jobject result = env->NewObject(
        cls,
        ctor,
        r.is_error,
        jstr(env, r.error_message),
        r.final_score,
        r.tp_score,
        r.tcp_score,
        r.media_score,
        records,
        proxy,
        counters
    );

    env->DeleteLocalRef(records);
    env->DeleteLocalRef(proxy);
    env->DeleteLocalRef(counters);

    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeQuit(
    JNIEnv*, jobject, jlong p
) {
    ptr(p)->quit();
}

extern "C" JNIEXPORT void JNICALL
Java_nay_kirill_tdconnection_native_TdConnectionNative_nativeDestroy(
    JNIEnv*, jobject, jlong p
) {
    auto* conn = reinterpret_cast<TdConnection*>(p);
    delete conn;
}
