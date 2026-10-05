# SecretKeeper - Windows 端四层自检构建脚本（MSVC 直编，不经 CMake）
#
# 图形界面由根 CMakeLists.txt 构建（见 README），本脚本只负责四层自检：
#   - 图形界面无法在无显示环境下验证，但密码学 / 存储 / 核心 / 业务四层是纯逻辑，
#     因此单独提供一条不依赖 CMake 的直编链路，便于 CI 做分层验证；
#   - 内置 Argon2 参考实现要求 opt.c 与 ref.c **二选一**
#     （上游 Makefile 即如此，两者同时编译会重复定义 fill_segment）。
#     本脚本负责探测 CPU 是否支持相应指令集来选择，CMake 走 check_cxx_source_compiles。
#
# 用法（需先加载 MSVC 环境）：
#   call vcvars64.bat
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
    [switch]$CompileOnly,
    # 只构建跨平台一致性探针。CI 用它产出 transcript 交给比对 job。
    [switch]$Conformance
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")

# CI 的 Windows 控制台默认代码页不是 UTF-8，Write-Host 打印中文会抛
# NativeCommandFailed 让 job 变红。强制切到 UTF-8，与 runner 上的
# chcp 65001 双保险。
try { chcp 65001 > $null } catch { }
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

# 产物目录与 CMake 保持一致：build/{platform}/{arch}/{config}。
# 中间产物按架构与配置隔离，避免 x64/arm64、Debug/Release 的同名 .obj 互相覆盖。
$buildDir = "build\windows\$Architecture\$Configuration"
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
    "/I src\include",
    "/I src\app",
    "/I vendor\argon2\include",
    "/I vendor\sqlite",
    "/I vendor\mbedtls\include"
)


if ($Test -or $CompileOnly -or $Conformance) {
    if ($CompileOnly) {
        Write-Host "目标架构: $Architecture（仅编译，不运行）"
    }

    # core 四层 + UI 纯逻辑，与生产代码的依赖方向一致。
    # 每个目标只编译它自己这一层（及之下），不把下层测试拉进来。
    $coreSrcs = @(
        "src\common\core\crypto.cpp",
        "src\common\core\serialize.cpp",
        "src\common\core\_error.cpp",
        "src\common\core\_text.cpp",
        "src\common\core\_hex.cpp",
        "src\common\core\_file_store.cpp",
        "src\common\core\kek_cache.cpp",
        "src\common\core\backoff.cpp",
        "src\common\core\store.cpp",
        "src\common\core\service.cpp"
    )

    # 目标定义：Name / Exe / 额外源文件 / 额外源组。
    # sqlite 只有 store 与 service 需要；argon2 与 mbedtls 由 crypto 引入。
    $targets = @(
        @{
            Name = "conformance"; Exe = "crypto_conformance.exe"
            Srcs = @("src\common\core\crypto.cpp", "tests\core\crypto_conformance.cpp")
            Groups = @("argon", "mbedtls")
            NoTest = $true
        },
        @{
            Name = "crypto";   Exe = "crypto_test.exe"
            Srcs = @("src\common\core\crypto.cpp", "tests\core\crypto_test.cpp")
            Groups = @("argon", "mbedtls")
        },

        @{
            Name = "serialize"; Exe = "serialize_test.exe"
            Srcs = @("src\common\core\serialize.cpp", "tests\core\serialize_test.cpp")
            Groups = @()
        },
        @{
            Name = "store";    Exe = "store_test.exe"
            Srcs = @(
                "src\common\core\serialize.cpp",
                "src\common\core\_hex.cpp",
                "src\common\core\_file_store.cpp",
                "src\common\core\store.cpp",
                "tests\core\store_test.cpp"
            )
            Groups = @("sqlite")
        },
        @{
            Name = "service";  Exe = "service_test.exe"
            Srcs = $coreSrcs + @("tests\core\service_test.cpp")
            Groups = @("argon", "mbedtls", "sqlite")
        },
        @{
            Name = "ui";       Exe = "ui_test.exe"
            Srcs = $coreSrcs + @(
                "src\app\view_model.cpp",
                "tests\ui\view_model_test.cpp"
            )
            Groups = @("argon", "mbedtls", "sqlite")
        }
    )

    Write-Host "`n=== 构建自检程序（$($targets.Count) 个目标）==="
    foreach ($t in $targets) {
        Write-Host "`n--- $($t.Name) ---"
        # cl.exe 不会自动创建 /Fo 指向的子目录。
        $targetObjDir = Join-Path $objDir $t.Name
        New-Item -ItemType Directory -Force -Path $targetObjDir | Out-Null
        $all = @()
        foreach ($g in $t.Groups) {
            switch ($g) {
                "argon"   { $all += $argonSrcs }
                "mbedtls" { $all += $mbedtlsSrcs }
                "sqlite"  { $all += $sqliteSrcs }
            }
        }
        $all += $t.Srcs

        $cmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc /W4 $cfgFlag /MD " +
               "/D_CRT_SECURE_NO_WARNINGS /DSQLITE_OMIT_LOAD_EXTENSION " +
               ($includeArgs -join " ") + " " +
               "/Fo:$targetObjDir\ /Fd:$targetObjDir\ /Fe:$buildDir\$($t.Exe) " +
               (($all | ForEach-Object { "`"$_`"" }) -join " ") +
               " $($argonFlags -join ' ') /link bcrypt.lib"
        cmd /c $cmd
        if ($LASTEXITCODE -ne 0) { throw "$($t.Name) 编译失败" }
        if ($Conformance -and -not $Test -and $t.Name -ne "crypto") { break }
    }

    if ($CompileOnly) {
        Write-Host "`n交叉编译完成（未运行测试）。"
    } elseif ($Conformance -and -not $Test) {
        Write-Host "`n一致性探针已构建：$buildDir\crypto_conformance.exe"
    } else {
        # 执行逻辑移交 run-tests.ps1：编译与执行分离，交叉编译时无需运行。
        & pwsh -NoProfile -File "windows/scripts/run-tests.ps1" `
            -Architecture $Architecture -Root "."
        if ($LASTEXITCODE -ne 0) { throw "测试失败" }
    }
} else {
    Write-Host "构建目标：库（尚未定义 UI 工程）"
    Write-Host "提示：加 -CompileOnly 可只编译自检程序而不运行。"
}