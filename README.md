# TDLib Network Check Fork

This fork extends TDLib with Telegram network quality testing.

Original TDLib repository: [tdlib/td](https://github.com/tdlib/td)

The network check is a utility feature that estimates how stable and fast a Telegram client can work under current network conditions. It runs a sequence of measurements and returns a final score with detailed diagnostics.

All network check implementation code in this fork is located in `tg-connection-core/`.

## What Is Measured

- availability and latency to current Telegram DCs;
- TCP connection stability (success rate, p50/p95, jitter);
- MTProto response performance (via `testProxy`);
- Telegram media loading speed;
- indirect signs of unstable connectivity (frequent reconnects).

## Use Cases

- before sensitive flows (login, sync, media loading);
- to choose retry timing and strategy;
- to show network status and recommendations to users;
- for internal monitoring of real-world connection quality.

## Usage Examples

### C++ API usage

```cpp
#include "tg-connection-core/connection/TdConnection.h"

class MyLogger final : public ExternalLogger {
 public:
  void log(int level, const std::string &message) override {
    std::cout << "[" << level << "] " << message << std::endl;
  }
};

int main() {
  TdConnection::TdlibCredentials creds;
  creds.api_id = 123456;
  creds.api_hash = "your_api_hash";

  auto logger = std::make_unique<MyLogger>();
  TdConnection connection(std::move(logger), std::move(creds));

  connection.start("./td_db", "./td_files", "");

  ConnectionTestResult result = connection.check_connection(60000);
  std::cout << result.summary() << std::endl;

  connection.quit();
  return 0;
}
```

### Android JNI usage

Native `create` now accepts TDLib credentials (`apiId`, `apiHash`).

Kotlin/Java declaration example:

```kotlin
private external fun nativeCreate(
    nativeLogger: NativeLogger,
    apiId: Int,
    apiHash: String
): Long
```
Usage example:

```kotlin
val nativePtr = nativeCreate(logger, apiId, apiHash)
nativeStart(nativePtr, databaseDir, filesDir, caPath)
val result = nativeCheckConnection(nativePtr, 60_000)
```

## Android Build and Integration

### 1) Build OpenSSL and cURL for Android

From repository root:

```bash
cd tg-connection-core/android
./build-openssl.sh <ANDROID_SDK_ROOT> <ANDROID_NDK_VERSION> third-party/openssl OpenSSL_1_1_1w
```

Example:

```bash
./build-openssl.sh ~/Android/Sdk 23.2.8568313 third-party/openssl OpenSSL_1_1_1w
```

This also builds cURL into `tg-connection-core/android/third-party/curl/<abi>/...`.

### 2) Build Android TDLib/JNI binaries

```bash
./build-tdlib.sh <ANDROID_SDK_ROOT> <ANDROID_NDK_VERSION> third-party/openssl c++_static Java
```

Example:

```bash
./build-tdlib.sh ~/Android/Sdk 23.2.8568313 third-party/openssl c++_static Java
```

### 3) Get resulting binaries

Main output directory:

- `tg-connection-core/android/tdlib/libs/<abi>/`

Artifacts include:

- `libtdjni.so` (your JNI bridge with network check);
- `libtd*.so` produced by TDLib build;
- optionally `libcrypto.so` and `libssl.so` if shared OpenSSL was built;
- `libc++_shared.so` if built with `c++_shared`.

Additionally, packaged archives are created in:

- `tg-connection-core/android/tdlib/tdlib.zip`
- `tg-connection-core/android/tdlib/tdlib-debug.zip`

### 4) Integrate into Android app

Copy all `.so` files per ABI into your Android module:

- `app/src/main/jniLibs/arm64-v8a/*.so`
- `app/src/main/jniLibs/armeabi-v7a/*.so`
- `app/src/main/jniLibs/x86_64/*.so`
- `app/src/main/jniLibs/x86/*.so`

Then:

- load native library in app startup (usually `System.loadLibrary("tdjni")`);
- pass `apiId` and `apiHash` to `nativeCreate(...)`;
- call `nativeStart(...)` and `nativeCheckConnection(...)` from your wrapper.

## C++ Example Build and Run

### 1) Prepare credentials file

Create `tg-connection-core/cpp/tdlib_credentials.env` from example:

```bash
cp tg-connection-core/cpp/tdlib_credentials.env.example tg-connection-core/cpp/tdlib_credentials.env
```

Set values:

```env
API_ID=123456
API_HASH=your_api_hash_here
```

### 2) Build

From repository root:

```bash
cmake -S tg-connection-core/cpp -B tg-connection-core/cpp/build-local
cmake --build tg-connection-core/cpp/build-local --config Release -j
```

### 3) Run

```bash
./tg-connection-core/cpp/build-local/connection_test
```

Interactive commands:

- `c` runs connection test;
- `q` quits.
