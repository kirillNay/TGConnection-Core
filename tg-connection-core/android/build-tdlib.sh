#!/usr/bin/env bash

ANDROID_SDK_ROOT=${1:-SDK}
ANDROID_NDK_VERSION=${2:-23.2.8568313}
OPENSSL_INSTALL_DIR=${3:-third-party/openssl}
ANDROID_STL=${4:-c++_static}
TDLIB_INTERFACE=${5:-Java}

if [ "$ANDROID_STL" != "c++_static" ] && [ "$ANDROID_STL" != "c++_shared" ] ; then
  echo 'Error: ANDROID_STL must be either "c++_static" or "c++_shared".'
  exit 1
fi

if [ "$TDLIB_INTERFACE" != "Java" ] && [ "$TDLIB_INTERFACE" != "JSON" ] && [ "$TDLIB_INTERFACE" != "JSONJava" ] ; then
  echo 'Error: TDLIB_INTERFACE must be either "Java", "JSON", or "JSONJava".'
  exit 1
fi

source ./check-environment.sh || exit 1

if [ ! -d "$ANDROID_SDK_ROOT" ] ; then
  echo "Error: directory \"$ANDROID_SDK_ROOT\" doesn't exist. Run ./fetch-sdk.sh first, or provide a valid path to Android SDK."
  exit 1
fi

if [ ! -d "$OPENSSL_INSTALL_DIR" ] ; then
  echo "Error: directory \"$OPENSSL_INSTALL_DIR\" doesn't exists. Run ./build-openssl.sh first."
  exit 1
fi

ANDROID_SDK_ROOT="$(cd "$(dirname -- "$ANDROID_SDK_ROOT")" >/dev/null; pwd -P)/$(basename -- "$ANDROID_SDK_ROOT")"
ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$ANDROID_NDK_VERSION"
OPENSSL_INSTALL_DIR="$(cd "$(dirname -- "$OPENSSL_INSTALL_DIR")" >/dev/null; pwd -P)/$(basename -- "$OPENSSL_INSTALL_DIR")"
PATH=$ANDROID_SDK_ROOT/cmake/3.22.1/bin:$PATH
TDLIB_INTERFACE_OPTION=$([ "$TDLIB_INTERFACE" == "JSON" ] && echo "-DTD_ANDROID_JSON=ON" || [ "$TDLIB_INTERFACE" == "JSONJava" ] && echo "-DTD_ANDROID_JSON_JAVA=ON" || echo "")

cd $(dirname $0)

echo "Generating TDLib source files..."
mkdir -p build-native-$TDLIB_INTERFACE || exit 1
cd build-native-$TDLIB_INTERFACE
cmake $TDLIB_INTERFACE_OPTION -DTD_GENERATE_SOURCE_FILES=ON .. || exit 1
cmake --build . || exit 1
cd ..

rm -rf tdlib || exit 1

echo "Building TDLib..."
# for ABI in arm64-v8a ; do
for ABI in arm64-v8a armeabi-v7a x86_64 x86 ; do
  mkdir -p tdlib/libs/$ABI/ || exit 1

  mkdir -p build-$ABI-$TDLIB_INTERFACE || exit 1
  cd build-$ABI-$TDLIB_INTERFACE
  cmake -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" -DOPENSSL_ROOT_DIR="$OPENSSL_INSTALL_DIR/$ABI" -DCMAKE_BUILD_TYPE=RelWithDebInfo -GNinja -DANDROID_ABI=$ABI -DANDROID_STL=$ANDROID_STL -DANDROID_PLATFORM=android-16 $TDLIB_INTERFACE_OPTION .. || exit 1
  cmake --build . --target tdjni || exit 1
  cp -p libtd*.so* ../tdlib/libs/$ABI/ || exit 1
  cd ..

  if [[ "$ANDROID_STL" == "c++_shared" ]] ; then
    if [[ "$ABI" == "arm64-v8a" ]] ; then
      FULL_ABI="aarch64-linux-android"
    elif [[ "$ABI" == "armeabi-v7a" ]] ; then
      FULL_ABI="arm-linux-androideabi"
    elif [[ "$ABI" == "x86_64" ]] ; then
      FULL_ABI="x86_64-linux-android"
    elif [[ "$ABI" == "x86" ]] ; then
      FULL_ABI="i686-linux-android"
    fi
    cp "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/sysroot/usr/lib/$FULL_ABI/libc++_shared.so" tdlib/libs/$ABI/ || exit 1
    "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin/llvm-strip" tdlib/libs/$ABI/libc++_shared.so || exit 1
  fi
  if [ -e "$OPENSSL_INSTALL_DIR/$ABI/lib/libcrypto.so" ] ; then
    cp "$OPENSSL_INSTALL_DIR/$ABI/lib/libcrypto.so" "$OPENSSL_INSTALL_DIR/$ABI/lib/libssl.so" tdlib/libs/$ABI/ || exit 1
    "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin/llvm-strip" tdlib/libs/$ABI/libcrypto.so || exit 1
    "$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/$HOST_ARCH/bin/llvm-strip" tdlib/libs/$ABI/libssl.so || exit 1
  fi
done

echo "Compressing..."
rm -f tdlib.zip tdlib-debug.zip || exit 1
jar -cMf tdlib-debug.zip tdlib || exit 1
rm tdlib/libs/*/*.debug || exit 1
jar -cMf tdlib.zip tdlib || exit 1
mv tdlib.zip tdlib-debug.zip tdlib || exit 1

echo "Done."
