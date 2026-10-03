# SecretKeeper / 密匣 — 桌面三端统一构建入口
#
# 桌面三端（Windows / Linux / macOS）共享 src/common 与 src/include，
# 本文件负责把它们与 vendor 内置的密码学、图像、界面库一起编成可执行程序。
#
# 用法：
#   cmake -S . -B build/windows -A x64
#   cmake --build build/windows --config Release

cmake_minimum_required(VERSION 3.20)
project(SecretKeeper LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------- 平台判定
if(WIN32)
  set(SK_PLATFORM_WINDOWS ON)
elseif(APPLE)
  set(SK_PLATFORM_MACOS ON)
elseif(UNIX)
  set(SK_PLATFORM_LINUX ON)
else()
  message(FATAL_ERROR "不支持的平台")
endif()

# ---------------------------------------------------------------- 全局编译选项
add_compile_options(/utf-8)
if(MSVC)
  add_compile_options(/W4 /EHsc)
  # 产物为单文件 exe：静态运行库，无需随包分发 VC++ redistributable。
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------- 内置第三方
# ---- SQLite ----
add_library(sk_sqlite STATIC vendor/sqlite/sqlite3.c)
target_include_directories(sk_sqlite PUBLIC vendor/sqlite)
if(MSVC)
  target_compile_options(sk_sqlite PRIVATE /W0)
else()
  target_compile_options(sk_sqlite PRIVATE -w)
endif()

# ---- mbedTLS ----
# 已在 vendor/mbedtls/include/mbedtls/mbedtls_config.h 末尾的 override 块裁剪过，
# 因此只编译存活的编译单元（TLS / X.509 / net / file I/O 已关闭）。
add_library(sk_mbedtls STATIC
  vendor/mbedtls/library/aes.c
  vendor/mbedtls/library/aesni.c
  vendor/mbedtls/library/asn1parse.c
  vendor/mbedtls/library/asn1write.c
  vendor/mbedtls/library/bignum.c
  vendor/mbedtls/library/bignum_core.c
  vendor/mbedtls/library/base64.c
  vendor/mbedtls/library/cipher.c
  vendor/mbedtls/library/cipher_wrap.c
  vendor/mbedtls/library/constant_time.c
  vendor/mbedtls/library/ctr_drbg.c
  vendor/mbedtls/library/entropy.c
  vendor/mbedtls/library/entropy_poll.c
  vendor/mbedtls/library/gcm.c
  vendor/mbedtls/library/memory_buffer_alloc.c
  vendor/mbedtls/library/md.c
  vendor/mbedtls/library/oid.c
  vendor/mbedtls/library/pem.c
  vendor/mbedtls/library/pk.c
  vendor/mbedtls/library/pk_wrap.c
  vendor/mbedtls/library/pkparse.c
  vendor/mbedtls/library/pkwrite.c
  vendor/mbedtls/library/platform.c
  vendor/mbedtls/library/platform_util.c
  vendor/mbedtls/library/rsa.c
  vendor/mbedtls/library/rsa_alt_helpers.c
  vendor/mbedtls/library/sha256.c
  vendor/mbedtls/library/sha512.c
  vendor/mbedtls/library/version.c
)
target_include_directories(sk_mbedtls PUBLIC vendor/mbedtls/include)
if(MSVC)
  target_compile_options(sk_mbedtls PRIVATE /W0)
else()
  target_compile_options(sk_mbedtls PRIVATE -w)
endif()

# ---- Argon2id ----
# 上游要求 opt.c 与 ref.c 二选一：opt.c 按目标架构使用 SIMD，
# 不存在时回退 ref.c。CMake 用 check_cxx_source_compiles 判定。
include(CheckCXXSourceCompiles)
set(SK_ARGON2_IMPL "ref")
if(SK_PLATFORM_WINDOWS OR SK_PLATFORM_MACOS)
  if(MSVC)
    check_cxx_source_compiles("
      #include <immintrin.h>
      int main(){ __m128i a=_mm_setzero_si128(); (void)a; return 0; }" SK_ARGON2_HAS_SSE41)
    if(SK_ARGON2_HAS_SSE41)
      set(SK_ARGON2_IMPL "opt")
    endif()
  else()
    check_cxx_source_compiles("
      #include <immintrin.h>
      int main(){ __m128i a=_mm_setzero_si128(); (void)a; return 0; }" SK_ARGON2_HAS_SSE41)
    if(SK_ARGON2_HAS_SSE41)
      set(SK_ARGON2_IMPL "opt")
    endif()
  endif()
endif()

set(SK_ARGON2_SOURCES
  vendor/argon2/src/argon2.c
  vendor/argon2/src/core.c
  vendor/argon2/src/encoding.c
  vendor/argon2/src/thread.c
  vendor/argon2/src/blake2/blake2b.c
)
if(SK_ARGON2_IMPL STREQUAL "opt")
  list(APPEND SK_ARGON2_SOURCES vendor/argon2/src/opt.c)
else()
  list(APPEND SK_ARGON2_SOURCES vendor/argon2/src/ref.c)
endif()

add_library(sk_argon2 STATIC ${SK_ARGON2_SOURCES})
target_include_directories(sk_argon2 PUBLIC vendor/argon2/include)
if(SK_ARGON2_IMPL STREQUAL "opt" AND NOT MSVC)
  target_compile_options(sk_argon2 PRIVATE -msse4.1 -mavx2)
endif()
if(MSVC)
  target_compile_options(sk_argon2 PRIVATE /W0)
else()
  target_compile_options(sk_argon2 PRIVATE -w)
endif()

# ---- FLTK 1.4.5 ----
# 静态链接，产物不依赖任何第三方 DLL。裁剪纪律见 vendor/fltk/README.md。
set(FLTK_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_TEST         OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID        OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS        OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_HTML_DOCS    OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID_DOCS   OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID_HTML_DOCS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
# Windows 下必须显式关闭 GDI+，否则链接阶段需要 gdiplus.lib 与额外初始化。
set(FLTK_GRAPHICS_GDIPLUS   OFF CACHE BOOL "" FORCE)
set(FLTK_OPTION_STD         ON  CACHE BOOL "" FORCE)

# FLTK 自带一个 MSVC 运行库开关（默认 ON，即 /MD），并会覆盖
# CMAKE_MSVC_RUNTIME_LIBRARY。若不关掉，链接阶段必然报 LNK2038
# （fltk.lib 是 /MD，本工程是 /MT）。关闭后与全局的静态运行库一致。
set(FLTK_MSVC_RUNTIME_DLL OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/fltk)

# ---------------------------------------------------------------- 共享核心库
add_library(sk_core STATIC
  src/common/container.cpp
  src/common/crypto.cpp
  src/common/hex.cpp
  src/common/index_db.cpp
  src/common/file_store.cpp
  src/common/error.cpp
  src/common/backoff.cpp
  src/common/kek_cache.cpp
  src/common/text.cpp
  src/common/master_key_service.cpp
  src/common/secret_service.cpp
)
target_include_directories(sk_core PUBLIC src/include)
target_link_libraries(sk_core PUBLIC sk_mbedtls sk_argon2 sk_sqlite)
if(SK_PLATFORM_WINDOWS)
  # entropy_poll.c 通过 BCryptGenRandom 取平台熵。
  target_link_libraries(sk_core PUBLIC bcrypt)
endif()

# ---------------------------------------------------------------- 平台层 + 界面
# SK_BUILD_APP=OFF 时只构建共享核心与四层自检，用于 CI 的分层验证。
option(SK_BUILD_APP "构建图形界面可执行程序" ON)

if(SK_BUILD_APP)
  # 界面逻辑（src/app）三端完全共享；只有 platform.cpp 逐端不同，
  # 因为数据目录与文件对话框必须用各自的系统 API。
  if(SK_PLATFORM_WINDOWS)
    set(SK_PLATFORM_DIR windows)
    add_executable(SecretKeeper WIN32 src/app/app.cpp src/windows/platform.cpp)
  elseif(SK_PLATFORM_MACOS)
    set(SK_PLATFORM_DIR macos)
    add_executable(SecretKeeper src/app/app.cpp src/macos/platform.cpp)
  else()
    set(SK_PLATFORM_DIR linux)
    add_executable(SecretKeeper src/app/app.cpp src/linux/platform.cpp)
  endif()

  # app.cpp include 的是 "platform.h"，故平台目录必须排在共享头文件之前。
  target_include_directories(SecretKeeper PRIVATE src/${SK_PLATFORM_DIR} src/app)
  target_link_libraries(SecretKeeper PRIVATE sk_core fltk)
  if(APPLE)
    target_link_libraries(SecretKeeper PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
  endif()
endif()

# ---------------------------------------------------------------- 四层自检
# 由 CTest 驱动；亦可 cmake --build 后直接运行 build/<platform>/<cfg>/sk_*_test
enable_testing()

# crypto / store 两层要读仓库里的 test-vectors/，因此必须传入仓库根路径；
# core / service 两层的用例自包含，不需要参数。
add_executable(sk_crypto_test tests/crypto_test.cpp)
target_link_libraries(sk_crypto_test PRIVATE sk_core)
add_test(NAME crypto COMMAND sk_crypto_test "${CMAKE_SOURCE_DIR}")

add_executable(sk_store_test tests/store_test.cpp)
target_link_libraries(sk_store_test PRIVATE sk_core)
add_test(NAME store COMMAND sk_store_test "${CMAKE_SOURCE_DIR}")

add_executable(sk_core_test tests/core_test.cpp)
target_link_libraries(sk_core_test PRIVATE sk_core)
add_test(NAME core COMMAND sk_core_test)

add_executable(sk_service_test tests/service_test.cpp)
target_link_libraries(sk_service_test PRIVATE sk_core)
add_test(NAME service COMMAND sk_service_test)

message(STATUS "SecretKeeper 平台: Windows=${SK_PLATFORM_WINDOWS} Linux=${SK_PLATFORM_LINUX} macOS=${SK_PLATFORM_MACOS}")
message(STATUS "Argon2 实现: ${SK_ARGON2_IMPL}")
