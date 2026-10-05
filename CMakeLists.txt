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
# /utf-8 是 MSVC 专有开关，必须只在 MSVC 下加。源文件含中文注释，
# 不加则 MSVC 按 GBK 解析导致编译错误；但若无条件加到 GCC/Clang，
# 会被当成链接器输入文件而报 "linker input file not found"。
if(MSVC)
  add_compile_options(/utf-8 /W4 /EHsc)
  # 产物为单文件 exe：静态运行库，无需随包分发 VC++ redistributable。
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options(-Wall -Wextra)
endif()


# ---------------------------------------------------------------- 目标架构判定
# aesni.c 是 x86 AES-NI 汇编，aesce.c 是 Armv8 AES 指令。mbedtls_config.h 把
# MBEDTLS_AESNI_C 与 MBEDTLS_AESCE_C **同时**无条件 #define，若按 config 的字面
# 编译，在 x86 上缺 aesce_* 符号、在 ARM64 上缺 aesni_* 符号，两端都链接失败。
# 因此按目标架构只编入对应的那一个，其余用 -D 置 0 关掉相应路径。
# 判定目标架构不能只看 CMAKE_SYSTEM_PROCESSOR：它在交叉编译时不走
# toolchain 文件时可能仍是宿主架构（x86_64），导致 ARM64 目标误编 aesni.c。
# 以「宿主是否 Windows/Apple Silicon」结合 CMAKE_SYSTEM_PROCESSOR 双条件，
# 并允许 -DSK_TARGET_ARCH=arm64 显式覆盖。
# 判定目标架构不能只看 CMAKE_SYSTEM_PROCESSOR：交叉编译且不经 toolchain 文件时
# 它报告的仍是**宿主**架构（在 x86 上交叉编译 aarch64 时它是 x86_64），
# 于是 ARM64 目标会误编 aesni.c 并在链接期缺 aesce_* 符号。
# 因此优先读显式覆盖项，其次用编译器自身的目标三元组（最可靠），
# 最后才退回 CMAKE_SYSTEM_PROCESSOR。
if(DEFINED SK_TARGET_ARCH)
  set(SK_ARCH "${SK_TARGET_ARCH}")
else()
  set(SK_DETECTED_ARCH "")
  if(CMAKE_C_COMPILER)
    execute_process(COMMAND "${CMAKE_C_COMPILER}" -dumpmachine
                    OUTPUT_VARIABLE SK_CC_TRIPLE ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(SK_CC_TRIPLE MATCHES "aarch64|arm64")
      set(SK_DETECTED_ARCH "arm64")
    elseif(SK_CC_TRIPLE MATCHES "x86_64|amd64|i[36]86")
      set(SK_DETECTED_ARCH "x64")
    endif()
  endif()
  if(SK_DETECTED_ARCH STREQUAL "")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[36]86|x86)$")
      set(SK_DETECTED_ARCH "x64")
    else()
      set(SK_DETECTED_ARCH "arm64")
    endif()
  endif()
  set(SK_ARCH "${SK_DETECTED_ARCH}")
endif()

# ---------------------------------------------------------------- 产物目录布局
# 统一为 build/{platform}/{arch}/{Debug,Release}，vendor 内置组件再下沉一层 module。
# 多配置生成器（MSVC）会在 RUNTIME_OUTPUT_DIRECTORY 之后自动追加配置名，
# 所以这里只给到 {arch}；单配置生成器（Linux/macOS 的 Ninja）必须自己带上
# CMAKE_BUILD_TYPE，否则 Debug 与 Release 的产物会互相覆盖。
if(DEFINED SK_PLATFORM_WINDOWS)
  set(SK_PLATFORM_NAME "windows")
elseif(DEFINED SK_PLATFORM_MACOS)
  set(SK_PLATFORM_NAME "macos")
else()
  set(SK_PLATFORM_NAME "linux")
endif()

set(SK_OUTPUT_BASE "${CMAKE_SOURCE_DIR}/build/${SK_PLATFORM_NAME}/${SK_ARCH}")
set(SK_OUTPUT_DIR "${SK_OUTPUT_BASE}")
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(SK_OUTPUT_DIR "${SK_OUTPUT_BASE}/${CMAKE_BUILD_TYPE}")
endif()

# 库与可执行文件同层，便于直接从 build/{platform}/{arch}/{config} 运行测试。
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${SK_OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${SK_OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SK_OUTPUT_DIR}")

# vendor 内置组件的产物归到 vendor/{module}。
set(SK_VENDOR_OUTPUT_DIR "${SK_OUTPUT_DIR}/vendor")

# ---------------------------------------------------------------- 内置第三方
# ---- SQLite ----
add_library(sk_sqlite STATIC vendor/sqlite/sqlite3.c)
target_include_directories(sk_sqlite PUBLIC vendor/sqlite)
set_target_properties(sk_sqlite PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${SK_VENDOR_OUTPUT_DIR}/sqlite")
if(MSVC)
  target_compile_options(sk_sqlite PRIVATE /W0)
else()
  target_compile_options(sk_sqlite PRIVATE -w)
endif()

# ---- mbedTLS ----
# 已在 vendor/mbedtls/include/mbedtls/mbedtls_config.h 末尾的 override 块裁剪过，
# 因此只编译存活的编译单元（TLS / X.509 / net / file I/O 已关闭）。
# mbedTLS 的 library/aesni.c 是 x86 AES-NI 指令的汇编实现，在 ARM64
# （macos-15 / Apple Silicon、Linux aarch64）上既无法编译，其符号
# （mbedtls_aesce_*）也无人提供，会在链接阶段报 Undefined symbols。
# mbedtls_config.h 无条件 #define MBEDTLS_AESNI_C，因此这里必须按架构剔除，
# 且不能改动 vendor 文件本身。
set(SK_MBEDTLS_SOURCES
  vendor/mbedtls/library/aes.c
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

# 注意：此处必须先 add_library 再设 target 属性，否则 CMake 报
# "Cannot specify compile definitions for target which is not built by this project"。
if(SK_ARCH STREQUAL "x64")
  list(APPEND SK_MBEDTLS_SOURCES vendor/mbedtls/library/aesni.c)
  set(SK_MBEDTLS_ACCEL "aesni.c (x86 AES-NI)")
else()
  list(APPEND SK_MBEDTLS_SOURCES vendor/mbedtls/library/aesce.c)
  set(SK_MBEDTLS_ACCEL "aesce.c (Armv8 AES)")
endif()
add_library(sk_mbedtls STATIC ${SK_MBEDTLS_SOURCES})
if(NOT SK_ARCH STREQUAL "x64")
  # 关掉 x86 路径：aesni.c 未编入，其符号必须无人引用。
  target_compile_definitions(sk_mbedtls PUBLIC MBEDTLS_AESNI_C=0)
endif()
message(STATUS "mbedTLS 加速实现: ${SK_ARCH} -> ${SK_MBEDTLS_ACCEL}")
target_include_directories(sk_mbedtls PUBLIC vendor/mbedtls/include)
set_target_properties(sk_mbedtls PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${SK_VENDOR_OUTPUT_DIR}/mbedtls")
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
set_target_properties(sk_argon2 PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${SK_VENDOR_OUTPUT_DIR}/argon2")
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
# 本项目不用 OpenGL。FLTK_BUILD_GL 默认 ON，且其 find_path(OPENGL_INCLUDE_DIR)
# 探测失败时不会优雅降级，而是让 CMake Generate 阶段直接报
# "variables are used but set to NOTFOUND"。故显式关闭。
set(FLTK_BUILD_GL           OFF CACHE BOOL "" FORCE)
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


# FLTK 的目标在 add_subdirectory 的作用域内创建，因此必须逐个设置输出目录。
# 只保留本工程实际链接的库（fltk 依赖 fltk_images / fltk_png / fltk_z）。
set(SK_FLTK_OUTPUT_DIR "${SK_VENDOR_OUTPUT_DIR}/fltk")
foreach(_sk_fltk_target fltk fltk_images fltk_png fltk_jpeg fltk_z)
  if(TARGET ${_sk_fltk_target})
    set_target_properties(${_sk_fltk_target} PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY "${SK_FLTK_OUTPUT_DIR}")
  endif()
endforeach()

# ---------------------------------------------------------------- 共享核心库
# core 四层：crypto / serialize 互不依赖，store 只依赖 serialize，
# service 依赖三者并作为 core 的唯一对外门面。
add_library(sk_crypto STATIC src/common/core/crypto.cpp)
target_include_directories(sk_crypto PUBLIC src/include)
target_link_libraries(sk_crypto PUBLIC sk_mbedtls sk_argon2)
if(SK_PLATFORM_WINDOWS)
  # entropy_poll.c 通过 BCryptGenRandom 取平台熵。
  target_link_libraries(sk_crypto PUBLIC bcrypt)
endif()

add_library(sk_serialize STATIC src/common/core/serialize.cpp)
target_include_directories(sk_serialize PUBLIC src/include)

add_library(sk_store STATIC
  src/common/core/store.cpp
  src/common/core/_file_store.cpp
  src/common/core/_hex.cpp
)
target_include_directories(sk_store PUBLIC src/include)
target_link_libraries(sk_store PUBLIC sk_serialize sk_sqlite)

add_library(sk_core STATIC
  src/common/core/service.cpp
  src/common/core/_error.cpp
  src/common/core/backoff.cpp
  src/common/core/kek_cache.cpp
  src/common/core/_text.cpp
)
target_include_directories(sk_core PUBLIC src/include)
target_link_libraries(sk_core PUBLIC sk_store sk_crypto sk_mbedtls sk_argon2 sk_sqlite)
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
  # 桌面三端共享同一份界面代码，只有 platform.cpp 逐端不同。
  # src/app 下按职责分文件：theme 是视觉常量与绘制助手，widgets 是自绘控件，
  # pages 是九个页面，app.cpp 只负责页面路由与事件循环。
  set(SK_APP_SOURCES
    src/app/app.cpp
    src/app/theme.cpp
    src/app/widgets.cpp
    src/app/pages.cpp
    src/app/main_window.cpp
    src/app/view_model.cpp
  )
  if(SK_PLATFORM_WINDOWS)
    set(SK_PLATFORM_DIR windows)
    add_executable(SecretKeeper WIN32 ${SK_APP_SOURCES} src/windows/platform.cpp)
  elseif(SK_PLATFORM_MACOS)
    set(SK_PLATFORM_DIR macos)
    add_executable(SecretKeeper ${SK_APP_SOURCES} src/macos/platform.cpp)
  else()
    set(SK_PLATFORM_DIR linux)
    add_executable(SecretKeeper ${SK_APP_SOURCES} src/linux/platform.cpp)
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
# crypto 与 serialize 两层要读仓库里的 test-vectors/，因此必须传入仓库根路径；
# store / service / ui 三层的用例自包含，不需要参数。
# 每个测试目标只链接它所在层及其下层已验证的库，与生产代码的依赖方向一致。
add_executable(sk_crypto_test tests/core/crypto_test.cpp)
target_link_libraries(sk_crypto_test PRIVATE sk_crypto)
add_test(NAME crypto COMMAND sk_crypto_test "${CMAKE_SOURCE_DIR}")

# 跨平台一致性探针。不参与 pass/fail 判定，而是输出一份 transcript，
# 由 scripts/compare-conformance.py 比对各平台的输出。它跑的是实际交付的
# C++ 代码，因此证明的是「各端实现一致」，而 verify-vectors.py 证明的只是
# 「向量本身自洽」。
add_executable(sk_crypto_conformance tests/core/crypto_conformance.cpp)
target_link_libraries(sk_crypto_conformance PRIVATE sk_crypto)

add_executable(sk_serialize_test tests/core/serialize_test.cpp)
target_link_libraries(sk_serialize_test PRIVATE sk_serialize)
add_test(NAME serialize COMMAND sk_serialize_test "${CMAKE_SOURCE_DIR}")

add_executable(sk_store_test tests/core/store_test.cpp)
target_link_libraries(sk_store_test PRIVATE sk_store sk_sqlite)
add_test(NAME store COMMAND sk_store_test)

add_executable(sk_service_test tests/core/service_test.cpp)
target_link_libraries(sk_service_test PRIVATE sk_core)
add_test(NAME service COMMAND sk_service_test)

# UI 层的纯逻辑（列表投影、黄色问号判定、文案映射）不依赖 FLTK 事件循环。
add_executable(sk_ui_test tests/ui/view_model_test.cpp src/app/view_model.cpp)
target_link_libraries(sk_ui_test PRIVATE sk_core)
target_include_directories(sk_ui_test PRIVATE src/app)
add_test(NAME ui COMMAND sk_ui_test)

message(STATUS "SecretKeeper 平台: Windows=${SK_PLATFORM_WINDOWS} Linux=${SK_PLATFORM_LINUX} macOS=${SK_PLATFORM_MACOS}")
message(STATUS "Argon2 实现: ${SK_ARGON2_IMPL}")
