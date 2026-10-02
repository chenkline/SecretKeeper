# 机密心 - Windows 端构建脚本（MSVC / 无 CMake 依赖）
#
# 存在的原因：
#   1) 本机与部分 CI 环境没有 CMake，而项目要求不安装新工具；
#   2) 内置 Argon2 参考实现要求 opt.c 与 ref.c **二选一**
#      （上游 Makefile 即如此，两者同时编译会重复定义 fill_segment）。
#      本脚本负责探测 CPU 是否支持相应指令集来选择。
#
# 用法（需先加载 MSVC 环境）：
#   call vcvars64.bat
#   powershell -File windows/scripts/build.ps1
# 或直接：
#   powershell -File windows/scripts/build.ps1 -Test
#
# 架构：
#   -Architecture x64   （默认）本机原生构建并运行全部四层测试
#   -Architecture arm64 交叉编译。ARM64 二进制无法在 x64 宿主上执行，
#                       故必须配合 -CompileOnly 只编译不运行。

param(
    [string]$Configuration = "Release",
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [switch]$Test,
    [switch]$CompileOnly
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")

# CI 的 Windows 控制台默认代码页不是 UTF-8，Write-Host 打印中文会抛
# NativeCommandFailed 让 job 变红。强制切到 UTF-8，与 runner 上的
# chcp 65001 双保险。
try { chcp 65001 > $null } catch { }
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# 中间产物按架构隔离，避免 x64 与 arm64 的同名 .obj 互相覆盖。
$buildDir = "windows\build\$Architecture"
$objDir = Join-Path $buildDir "obj"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

function Test-CpuSupports([string]$feature) {
    try {
        $flags = (Get-CimInstance Win32_Processor).Architecture
    } catch { return $false }
    # 保守处理：本项目只需在 opt 可用时用它，否则一律走 ref。
    switch ($feature) {
        "avx512f" { return $false }
        "avx2"    { return $false }
        "sse41"   { return $true }
        default    { return $false }
    }
}

# ---- 选择 Argon2 实现 ----
if ($Architecture -eq "arm64") {
    # opt.c 内只有 __AVX512F__ / __AVX2__ 两条 x86 分支，ARM64 上必须走 ref.c。
    # 这不是性能取舍而是正确性要求：宿主 CPU 探测读的是开发机的能力，
    # 交叉编译时会误选 opt.c 并带上 x86 SIMD 宏，而上游 Makefile 明确禁止
    # 在非 x86 目标上编译 opt.c。
    $argonImpl = "ref.c"; $argonFlags = @()
    Write-Host "Argon2: ARM64 目标，强制使用可移植参考实现（ref.c）"
} elseif (Test-CpuSupports "avx512f") {
    $argonImpl = "opt.c"; $argonFlags = @("/D__AVX512F__")
    Write-Host "Argon2: 使用 AVX512 优化实现"
} elseif (Test-CpuSupports "avx2") {
    $argonImpl = "opt.c"; $argonFlags = @("/D__AVX2__")
    Write-Host "Argon2: 使用 AVX2 优化实现"
} elseif (Test-CpuSupports "sse41") {
    $argonImpl = "opt.c"; $argonFlags = @("/D__SSE4_1__")
    Write-Host "Argon2: 使用 SSE4.1 优化实现"
} else {
    $argonImpl = "ref.c"; $argonFlags = @()
    Write-Host "Argon2: 使用可移植参考实现"
}

$cfgFlag = if ($Configuration -eq "Debug") { "/Od /Zi" } else { "/O2" }

$argonSrcs = @(
    "vendor\argon2\src\argon2.c",
    "vendor\argon2\src\core.c",
    "vendor\argon2\src\encoding.c",
    "vendor\argon2\src\thread.c",
    "vendor\argon2\src\blake2\blake2b.c",
    "vendor\argon2\src\$argonImpl"
)

$sqliteSrcs = @(
    # SQLite 编译单元体积很大且默认警告很吵，关掉 W4 以免噪声淹没真实问题。
    "vendor\sqlite\sqlite3.c"
)

# mbedTLS, trimmed via the override block at the bottom of mbedtls_config.h:
# TLS / X.509 / net / file I/O are disabled, so these are the only translation
# units that survive the preprocessor. entropy_poll.c is needed for the platform
# entropy source that seeds the CTR-DRBG.
$mbedtlsSrcs = @(
    "vendor\mbedtls\library\aes.c",
    "vendor\mbedtls\library\aesni.c",
    "vendor\mbedtls\library\asn1parse.c",
    "vendor\mbedtls\library\asn1write.c",
    "vendor\mbedtls\library\bignum.c",
    "vendor\mbedtls\library\base64.c",
    "vendor\mbedtls\library\bignum_core.c",
    "vendor\mbedtls\library\cipher.c",
    "vendor\mbedtls\library\cipher_wrap.c",
    "vendor\mbedtls\library\constant_time.c",
    "vendor\mbedtls\library\ctr_drbg.c",
    "vendor\mbedtls\library\entropy.c",
    "vendor\mbedtls\library\entropy_poll.c",
    "vendor\mbedtls\library\gcm.c",
    "vendor\mbedtls\library\memory_buffer_alloc.c",
    "vendor\mbedtls\library\md.c",
    "vendor\mbedtls\library\oid.c",
    "vendor\mbedtls\library\pk.c",
    "vendor\mbedtls\library\pkparse.c",
    "vendor\mbedtls\library\pem.c",
    "vendor\mbedtls\library\pk_wrap.c",
    "vendor\mbedtls\library\pkwrite.c",
    "vendor\mbedtls\library\platform.c",
    "vendor\mbedtls\library\platform_util.c",
    "vendor\mbedtls\library\rsa.c",
    "vendor\mbedtls\library\rsa_alt_helpers.c",
    "vendor\mbedtls\library\sha256.c",
    "vendor\mbedtls\library\sha512.c",
    "vendor\mbedtls\library\version.c"
)

$includeArgs = @(
    "/I windows\src\crypto",
    "/I windows\src\store",
    "/I vendor\argon2\include",
    "/I vendor\sqlite",
    "/I vendor\mbedtls\include"
)


if ($Test -or $CompileOnly) {
    if ($CompileOnly) {
        Write-Host "目标架构: $Architecture（仅编译，不运行）"
    }
    Write-Host "`n=== 构建测试可执行文件 ==="
    $srcs = $argonSrcs + $mbedtlsSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\container\container.cpp",
        "windows\tests\crypto_test.cpp"
    )
    $cmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc /W4 $cfgFlag /MD " +
           "/D_CRT_SECURE_NO_WARNINGS " +
           ($includeArgs -join " ") + " " +
           "/Fo:$objDir\ /Fe:$buildDir\crypto_test.exe " +
           (($srcs | ForEach-Object { "`"$_`"" }) -join " ") +
           " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }


    Write-Host "`n=== 构建存储层自检 ==="
    $storeSrcs = $argonSrcs + $mbedtlsSrcs + $sqliteSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\core\text.cpp",
        "windows\src\core\error.cpp",
        "windows\src\core\backoff.cpp",
        "windows\src\core\kek_cache.cpp",
        "windows\src\core\master_key_service.cpp",
        "windows\src\core\secret_service.cpp",
        "windows\src\container\container.cpp",
        "windows\src\store\hex.cpp",
        "windows\src\store\index_db.cpp",
        "windows\src\store\file_store.cpp",
        "windows\tests\store_test.cpp"
    )
    $storeCmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc $cfgFlag /MD " +
                "/D_CRT_SECURE_NO_WARNINGS /DSQLITE_OMIT_LOAD_EXTENSION " +
                ($includeArgs -join " ") + " " +
                "/Fo:$objDir\ /Fd:$objDir\ /Fe:$buildDir\store_test.exe " +
                (($storeSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
                " /link bcrypt.lib"
    cmd /c $storeCmd
    if ($LASTEXITCODE -ne 0) { throw "存储层编译失败" }


    Write-Host "`n=== 构建核心层自检 ==="
    $coreSrcs = $argonSrcs + $mbedtlsSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\core\text.cpp",
        "windows\src\core\error.cpp",
        "windows\src\core\backoff.cpp",
        "windows\src\core\kek_cache.cpp",
        "windows\tests\core_test.cpp"
    )
    $coreCmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc $cfgFlag /MD " +
               "/D_CRT_SECURE_NO_WARNINGS " +
               ($includeArgs -join " ") + " " +
               "/Fo:$objDir\ /Fd:$objDir\ /Fe:$buildDir\core_test.exe " +
               (($coreSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
               " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $coreCmd
    if ($LASTEXITCODE -ne 0) { throw "核心层编译失败" }


    Write-Host "`n=== 构建业务层自检 ==="
    $svcSrcs = $argonSrcs + $mbedtlsSrcs + $sqliteSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\container\container.cpp",
        "windows\src\core\text.cpp",
        "windows\src\core\error.cpp",
        "windows\src\core\backoff.cpp",
        "windows\src\core\kek_cache.cpp",
        "windows\src\core\master_key_service.cpp",
        "windows\src\core\secret_service.cpp",
        "windows\src\store\hex.cpp",
        "windows\src\store\index_db.cpp",
        "windows\src\store\file_store.cpp",
        "windows\tests\service_test.cpp"
    )
    $svcCmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc $cfgFlag /MD " +
              "/D_CRT_SECURE_NO_WARNINGS /DSQLITE_OMIT_LOAD_EXTENSION " +
              ($includeArgs -join " ") + " " +
              "/Fo:$objDir\ /Fd:$objDir\ /Fe:$buildDir\service_test.exe " +
              (($svcSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
              " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $svcCmd
    if ($LASTEXITCODE -ne 0) { throw "业务层编译失败" }

    if ($CompileOnly) {
        Write-Host "`n交叉编译完成（未运行测试）。"
    } else {
        # 执行逻辑移交 run-tests.ps1：编译与执行分离，交叉编译时无需运行。
        & pwsh -NoProfile -File "windows/scripts/run-tests.ps1" `
            -Architecture $Architecture -Root "."
        if ($LASTEXITCODE -ne 0) { throw "测试失败" }
    }
} else {
    Write-Host "构建目标：库（尚未定义 UI 工程）"
    Write-Host "提示：加 -CompileOnly 可只编译四层自检程序而不运行。"
}
