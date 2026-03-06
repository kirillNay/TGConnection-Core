#!/usr/bin/env bash

ANDROID_SDK_ROOT=${1:-SDK}
ANDROID_NDK_VERSION=${2:-23.2.8568313}
OPENSSL_INSTALL_DIR=${3:-third-party/openssl}
OPENSSL_VERSION=${4:-OpenSSL_1_1_1w} # openssl-3.3.0
BUILD_SHARED_LIBS=$5

CURL_INSTALL_DIR=${5:-third-party/curl}
CURL_VERSION=${6:-curl-8.7.1}
BUILD_SHARED_LIBS=$7

if [ ! -d "$ANDROID_SDK_ROOT" ] ; then
  echo "Error: directory \"$ANDROID_SDK_ROOT\" doesn't exist. Run ./fetch-sdk.sh first, or provide a valid path to Android SDK."
  exit 1
fi

if [ -e "$OPENSSL_INSTALL_DIR" ] ; then
  echo "Error: file or directory \"$OPENSSL_INSTALL_DIR\" already exists. Delete it manually to proceed."
  exit 1
fi

source ./check-environment.sh || exit 1

if [[ "$OS_NAME" == "win" ]] && [[ "$BUILD_SHARED_LIBS" ]] ; then
  echo "Error: OpenSSL shared libraries can't be built on Windows because of 'The command line is too long.' error during build. You can run the script in WSL instead."
  exit 1
fi

mkdir -p $OPENSSL_INSTALL_DIR || exit 1

ANDROID_SDK_ROOT="$(cd "$(dirname -- "$ANDROID_SDK_ROOT")" >/dev/null; pwd -P)/$(basename -- "$ANDROID_SDK_ROOT")"
OPENSSL_INSTALL_DIR="$(cd "$(dirname -- "$OPENSSL_INSTALL_DIR")" >/dev/null; pwd -P)/$(basename -- "$OPENSSL_INSTALL_DIR")"

cd $(dirname $0)

echo "Downloading OpenSSL sources..."
rm -f $OPENSSL_VERSION.tar.gz || exit 1
$WGET https://github.com/openssl/openssl/archive/refs/tags/$OPENSSL_VERSION.tar.gz || exit 1
rm -rf ./openssl-$OPENSSL_VERSION || exit 1
tar xzf $OPENSSL_VERSION.tar.gz || exit 1
rm $OPENSSL_VERSION.tar.gz || exit 1
cd openssl-$OPENSSL_VERSION

export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION  # for OpenSSL 3.*.*
export ANDROID_NDK_HOME=$ANDROID_NDK_ROOT                           # for OpenSSL 1.1.1
PATH=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin:$PATH

if ! clang --help >/dev/null 2>&1 ; then
  echo "Error: failed to run clang from Android NDK."
  if [[ "$OS_NAME" == "linux" ]] ; then
    echo "Prebuilt Android NDK binaries are linked against glibc, so glibc must be installed."
  fi
  exit 1
fi

ANDROID_API32=16
ANDROID_API64=21
if [[ ${ANDROID_NDK_VERSION%%.*} -ge 24 ]] ; then
  ANDROID_API32=19
fi
if [[ ${ANDROID_NDK_VERSION%%.*} -ge 26 ]] ; then
  ANDROID_API32=21
fi

SHARED_BUILD_OPTION=$([ "$BUILD_SHARED_LIBS" ] && echo "shared" || echo "no-shared")

for ABI in arm64-v8a armeabi-v7a x86_64 x86 ; do
  if [[ $ABI == "x86" ]] ; then
    ./Configure android-x86 ${SHARED_BUILD_OPTION} -U__ANDROID_API__ -D__ANDROID_API__=$ANDROID_API32 || exit 1
  elif [[ $ABI == "x86_64" ]] ; then
    LDFLAGS=-Wl,-z,max-page-size=16384 ./Configure android-x86_64 ${SHARED_BUILD_OPTION} -U__ANDROID_API__ -D__ANDROID_API__=$ANDROID_API64 || exit 1
  elif [[ $ABI == "armeabi-v7a" ]] ; then
    ./Configure android-arm ${SHARED_BUILD_OPTION} -U__ANDROID_API__ -D__ANDROID_API__=$ANDROID_API32 -D__ARM_MAX_ARCH__=8 || exit 1
  elif [[ $ABI == "arm64-v8a" ]] ; then
    LDFLAGS=-Wl,-z,max-page-size=16384 ./Configure android-arm64 ${SHARED_BUILD_OPTION} -U__ANDROID_API__ -D__ANDROID_API__=$ANDROID_API64 || exit 1
  fi

  sed -i.bak 's/-O3/-O3 -ffunction-sections -fdata-sections/g' Makefile || exit 1

  make depend -s || exit 1
  make -j4 -s || exit 1

  mkdir -p $OPENSSL_INSTALL_DIR/$ABI/lib/ || exit 1
  if [ "$BUILD_SHARED_LIBS" ] ; then
    cp libcrypto.so libssl.so $OPENSSL_INSTALL_DIR/$ABI/lib/ || exit 1
  else
    cp libcrypto.a libssl.a $OPENSSL_INSTALL_DIR/$ABI/lib/ || exit 1
  fi
  cp -r include $OPENSSL_INSTALL_DIR/$ABI/ || exit 1

  make distclean || exit 1
done

cd ..

rm -rf ./openssl-$OPENSSL_VERSION || exit 1

# -------------------------------
# Build CURL against built OpenSSL
# -------------------------------
if [ -e "$CURL_INSTALL_DIR" ] ; then
  echo "Error: file or directory \"$CURL_INSTALL_DIR\" already exists. Delete it manually to proceed."
  exit 1
fi
mkdir -p "$CURL_INSTALL_DIR" || exit 1
CURL_INSTALL_DIR="$(cd "$(dirname -- "$CURL_INSTALL_DIR")" >/dev/null; pwd -P)/$(basename -- "$CURL_INSTALL_DIR")"

echo "Downloading CURL sources..."

cd "$(dirname "$0")"

CURL_TARBALL="${CURL_VERSION}.tar.gz"
CURL_URL="https://curl.se/download/${CURL_TARBALL}"

rm -f "${CURL_TARBALL}"

echo "Downloading: ${CURL_URL}"
curl -fL --retry 3 --retry-delay 1 -o "${CURL_TARBALL}" "${CURL_URL}" || exit 1

rm -rf "./${CURL_VERSION}" || exit 1
tar xzf "${CURL_TARBALL}" || exit 1
rm "${CURL_TARBALL}" || exit 1

cd "${CURL_VERSION}" || exit 1

if [[ -z "${ANDROID_NDK_ROOT:-}" ]]; then
  echo "Error: ANDROID_NDK_ROOT is empty"
  exit 1
fi
if [[ ! -f "$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" ]]; then
  echo "Error: toolchain not found at: $ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"
  echo "ANDROID_NDK_ROOT=$ANDROID_NDK_ROOT"
  exit 1
fi

# CMake toolchain from NDK
CMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake"

# Minimize dependencies: HTTP/2 OFF, brotli OFF, zstd OFF, etc.
# Use OpenSSL from OPENSSL_INSTALL_DIR/<abi>
for ABI in arm64-v8a armeabi-v7a x86_64 x86 ; do
  echo "Building CURL for ABI=$ABI..."

  rm -rf build-$ABI || exit 1
  mkdir -p build-$ABI || exit 1
  cd build-$ABI || exit 1

  # API level: 21 for 64-bit, 16/19/21 for 32-bit as in your setup
  ANDROID_API=$ANDROID_API32
  if [[ $ABI == "arm64-v8a" || $ABI == "x86_64" ]] ; then
    ANDROID_API=$ANDROID_API64
  fi

  # OpenSSL paths (includes are in .../<abi>/include, libs in .../<abi>/lib)
  SSL_ROOT="$OPENSSL_INSTALL_DIR/$ABI"
  SSL_INCLUDE="$SSL_ROOT/include"
  SSL_LIB="$SSL_ROOT/lib"

  # curl linking type
  CURL_BUILD_SHARED=$([ "$BUILD_SHARED_LIBS" ] && echo "ON" || echo "OFF")
  CURL_BUILD_STATIC=$([ "$BUILD_SHARED_LIBS" ] && echo "OFF" || echo "ON")

  cmake .. \
    -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-$ANDROID_API \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$CURL_INSTALL_DIR/$ABI" \
    -DBUILD_SHARED_LIBS=$CURL_BUILD_SHARED \
    -DBUILD_STATIC_LIBS=$CURL_BUILD_STATIC \
    -DCURL_USE_OPENSSL=ON \
    -DOPENSSL_ROOT_DIR="$SSL_ROOT" \
    -DOPENSSL_INCLUDE_DIR="$SSL_INCLUDE" \
    -DOPENSSL_SSL_LIBRARY="$SSL_LIB/libssl.$([ "$BUILD_SHARED_LIBS" ] && echo "so" || echo "a")" \
    -DOPENSSL_CRYPTO_LIBRARY="$SSL_LIB/libcrypto.$([ "$BUILD_SHARED_LIBS" ] && echo "so" || echo "a")" \
    -DHTTP_ONLY=ON \
    -DCURL_DISABLE_FTP=ON \
    -DCURL_DISABLE_LDAP=ON \
    -DCURL_DISABLE_LDAPS=ON \
    -DCURL_DISABLE_TELNET=ON \
    -DCURL_DISABLE_DICT=ON \
    -DCURL_DISABLE_FILE=ON \
    -DCURL_DISABLE_IMAP=ON \
    -DCURL_DISABLE_POP3=ON \
    -DCURL_DISABLE_RTSP=ON \
    -DCURL_DISABLE_SMB=ON \
    -DCURL_DISABLE_SMTP=ON \
    -DCURL_DISABLE_GOPHER=ON \
    -DUSE_NGHTTP2=OFF \
    -DCURL_BROTLI=OFF \
    -DCURL_ZSTD=OFF \
    -DCURL_USE_LIBPSL=OFF \
    -DENABLE_THREADED_RESOLVER=ON \
    -DBUILD_CURL_EXE=OFF \
    -DCMAKE_C_FLAGS="-D_FILE_OFFSET_BITS=64" \
    -DCMAKE_CXX_FLAGS="-D_FILE_OFFSET_BITS=64" \
    -DBUILD_TESTING=OFF || exit 1

  cmake --build . --config Release || exit 1
  cmake --install . || exit 1

  cd .. || exit 1

  # Put libraries into the usual <abi>/lib structure
  mkdir -p "$CURL_INSTALL_DIR/$ABI/lib" || exit 1
  if [ "$BUILD_SHARED_LIBS" ] ; then
    # Usually cmake install already puts libcurl.so into <prefix>/lib, but just in case:
    if [ -f "$CURL_INSTALL_DIR/$ABI/lib/libcurl.so" ] ; then
      echo "libcurl.so already installed"
    fi
  else
    if [ -f "$CURL_INSTALL_DIR/$ABI/lib/libcurl.a" ] ; then
      echo "libcurl.a already installed"
    fi
  fi
done

cd ..
rm -rf ./${CURL_VERSION} || exit 1
