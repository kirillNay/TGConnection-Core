cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(TdAndroid VERSION 1.0 LANGUAGES CXX)

set(TD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

option(TD_ANDROID_JSON "Use \"ON\" to build JSON interface.")
option(TD_ANDROID_JSON_JAVA "Use \"ON\" to build Java wrapper for JSON API.")

if (CMAKE_CROSSCOMPILING)
  set(CMAKE_MODULE_PATH "${TD_DIR}/CMake")

  include(TdSetUpCompiler)
  td_set_up_compiler()
  string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -flto=thin -Oz")

  list(APPEND CMAKE_FIND_ROOT_PATH "${OPENSSL_ROOT_DIR}")
  add_subdirectory(${TD_DIR} td)

  add_library(tdjni SHARED 
    "${TD_DIR}/tg-connection-core/android/td_connection_jni.cpp"
    "${TD_DIR}/tg-connection-core/connection/TdConnection.cpp"
    "${TD_DIR}/tg-connection-core/connection/Record.cpp"
    "${TD_DIR}/tg-connection-core/connection/ConnectionTestResult.cpp"
    "${TD_DIR}/tg-connection-core/connection/TestProxyResult.cpp"
    "${TD_DIR}/tg-connection-core/connection/StatsUtils.cpp"
  )

  # -----------------------------
  # Static CURL
  # -----------------------------
  # OPENSSL_ROOT_DIR expected to point to .../openssl/<abi>
  # We'll derive ABI from ANDROID_ABI and use same layout for curl.
  if (NOT DEFINED ANDROID_ABI)
    message(FATAL_ERROR "ANDROID_ABI is not set (expected when cross-compiling for Android).")
  endif()

  # Base dir where your prebuilt curl lives (adjust if needed)
  set(CURL_ROOT_BASE "${TD_DIR}/tg-connection-core/android/third-party/curl")
  set(CURL_ROOT_DIR "${CURL_ROOT_BASE}/${ANDROID_ABI}")

  # Include dirs
  target_include_directories(tdjni PRIVATE
    "${CURL_ROOT_DIR}/include"
    "${OPENSSL_ROOT_DIR}/include"
  )

  # Imported static libs
  add_library(curl_static STATIC IMPORTED)
  set_target_properties(curl_static PROPERTIES
    IMPORTED_LOCATION "${CURL_ROOT_DIR}/lib/libcurl.a"
  )

  # Sanity checks (fail fast with clear message)
  if (NOT EXISTS "${CURL_ROOT_DIR}/lib/libcurl.a")
    message(FATAL_ERROR "libcurl.a not found: ${CURL_ROOT_DIR}/lib/libcurl.a")
  endif()

  target_link_libraries(tdjni PRIVATE
    Td::TdStatic
    curl_static
    log
    z
  )

  target_compile_definitions(tdjni PRIVATE PACKAGE_NAME="org/drinkless/tdlib")

  add_custom_command(TARGET tdjni POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rename $<TARGET_FILE:tdjni> $<TARGET_FILE:tdjni>.debug
    COMMAND ${CMAKE_STRIP} --strip-debug --strip-unneeded $<TARGET_FILE:tdjni>.debug -o $<TARGET_FILE:tdjni>)
else()
  add_subdirectory(${TD_DIR} td)

  if (TD_ANDROID_JSON_JAVA)
    return()
  endif()

  set(TD_API_JAVA_PACKAGE "org/drinkless/tdlib")
  set(TD_API_JAVA_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${TD_API_JAVA_PACKAGE}/TdApi.java")
  set(TD_API_TLO_PATH "${TD_DIR}/td/generate/auto/tlo/td_api.tlo")
  set(TD_API_TL_PATH "${TD_DIR}/td/generate/scheme/td_api.tl")
  set(JAVADOC_TL_DOCUMENTATION_GENERATOR_PATH "${TD_DIR}/td/generate/JavadocTlDocumentationGenerator.php")
  set(GENERATE_JAVA_CMD td_generate_java_api TdApi ${TD_API_TLO_PATH} ${CMAKE_CURRENT_SOURCE_DIR} ${TD_API_JAVA_PACKAGE})
  if (PHP_EXECUTABLE)
    set(GENERATE_JAVA_CMD ${GENERATE_JAVA_CMD} &&
        ${PHP_EXECUTABLE} ${JAVADOC_TL_DOCUMENTATION_GENERATOR_PATH} ${TD_API_TL_PATH} ${TD_API_JAVA_PATH} androidx.annotation.Nullable @Nullable &&
        ${PHP_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/AddIntDef.php ${TD_API_JAVA_PATH})
  endif()

  file(MAKE_DIRECTORY ${TD_API_JAVA_PACKAGE})
  add_custom_target(tl_generate_java
    COMMAND ${GENERATE_JAVA_CMD}
    COMMENT "Generate Java TL source files"
    DEPENDS td_generate_java_api tl_generate_tlo ${TD_API_TLO_PATH} ${TD_API_TL_PATH}
  )
endif()
