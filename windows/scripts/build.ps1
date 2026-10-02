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

param(
    [string]$Configuration = "Release",
    [switch]$Test
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")

# CI 的 Windows 控制台默认代码页不是 UTF-8，Write-Host 打印中文会抛
# NativeCommandFailed 让 job 变红。强制切到 UTF-8，与 runner 上的
# chcp 65001 双保险。
try { chcp 65001 > $null } catch { }
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$buildDir = "windows\build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

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
if (Test-CpuSupports "avx512f") {
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

if ($Test) {
    Write-Host "`n=== 构建测试可执行文件 ==="
    $srcs = $argonSrcs + $mbedtlsSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\container\container.cpp",
        "windows\tests\crypto_test.cpp"
    )
    $cmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc /W4 $cfgFlag /MD " +
           "/D_CRT_SECURE_NO_WARNINGS " +
           ($includeArgs -join " ") + " " +
           "/Fo:$buildDir\ /Fe:$buildDir\crypto_test.exe " +
           (($srcs | ForEach-Object { "`"$_`"" }) -join " ") +
           " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }

    Write-Host "`n=== 运行密码学层自检 ==="
    & "$buildDir\crypto_test.exe" "."
    $rc = $LASTEXITCODE
    if ($rc -ne 0) { throw "测试失败（退出码 $rc）" }

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
                "/Fo:$buildDir\ /Fd:$buildDir\store\ /Fe:$buildDir\store_test.exe " +
                (($storeSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
                " /link bcrypt.lib"
    cmd /c $storeCmd
    if ($LASTEXITCODE -ne 0) { throw "存储层编译失败" }

    & "$buildDir\store_test.exe" "."
    $storeRc = $LASTEXITCODE
    if ($storeRc -ne 0) { throw "存储层测试失败（退出码 $storeRc）" }

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
               "/Fo:$buildDir\ /Fd:$buildDir\store\ /Fe:$buildDir\core_test.exe " +
               (($coreSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
               " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $coreCmd
    if ($LASTEXITCODE -ne 0) { throw "核心层编译失败" }

    & "$buildDir\core_test.exe"
    $coreRc = $LASTEXITCODE
    if ($coreRc -ne 0) { throw "核心层测试失败（退出码 $coreRc）" }

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
              "/Fo:$buildDir\ /Fd:$buildDir\store\ /Fe:$buildDir\service_test.exe " +
              (($svcSrcs | ForEach-Object { "`"$_`"" }) -join " ") +
              " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $svcCmd
    if ($LASTEXITCODE -ne 0) { throw "业务层编译失败" }

    & "$buildDir\service_test.exe"
    $svcRc = $LASTEXITCODE
    if ($svcRc -ne 0) { throw "业务层测试失败（退出码 $svcRc）" }
    Write-Host "`n全部通过。"
} else {
    Write-Host "构建目标：库（尚未定义 UI 工程）"
}
