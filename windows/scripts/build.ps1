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

if ($Test) {
    Write-Host "`n=== 构建测试可执行文件 ==="
    $srcs = $argonSrcs + @(
        "windows\src\crypto\crypto.cpp",
        "windows\src\crypto\der.cpp",
        "windows\src\crypto\bignum.cpp",
        "windows\src\container\container.cpp",
        "windows\tests\crypto_test.cpp"
    )
    $cmd = "cl.exe /nologo /std:c++20 /utf-8 /EHsc /W4 $cfgFlag /MD " +
           "/D_CRT_SECURE_NO_WARNINGS " +
           "/I windows\src\crypto /I vendor\argon2\include " +
           "/Fo:$buildDir\ /Fe:$buildDir\crypto_test.exe " +
           (($srcs | ForEach-Object { "`"$_`"" }) -join " ") +
           " $($argonFlags -join ' ') /link bcrypt.lib"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "编译失败" }

    Write-Host "`n=== 运行密码学层自检 ==="
    & "$buildDir\crypto_test.exe" "."
    $rc = $LASTEXITCODE
    if ($rc -ne 0) { throw "测试失败（退出码 $rc）" }
    Write-Host "`n全部通过。"
} else {
    Write-Host "构建目标：库（尚未定义 UI 工程）"
}
