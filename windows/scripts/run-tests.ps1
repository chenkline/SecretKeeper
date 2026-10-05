# SecretKeeper - 测试驱动脚本
#
# 用途：把「编译」与「执行测试」分开。build.ps1 负责产出五个自检程序，
# 本脚本负责按正确顺序、带上正确参数逐个执行，并汇总结果。
#
# 拆分理由：CI 与本地都需要「只编译不运行」（ARM64 交叉编译、UI 构建）
# 和「运行测试」两种模式。混在一个脚本里会导致交叉编译时试图执行
# 目标架构的二进制。
#
# 用法（需先加载 MSVC 环境）：
#   pwsh -NoProfile -File windows/scripts/run-tests.ps1 -Architecture x64
#
# 退出码：0 = 全部通过；非 0 = 有测试失败。

param(
    [ValidateSet("x64", "arm64")]
    [string]$Architecture = "x64",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..\..")

try { chcp 65001 > $null } catch { }
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)

$buildDir = "build\windows\$Architecture\$Configuration"

# 五个自检程序及其调用方式，顺序与依赖方向一致（每层只测本层及之下）。
#
# root 参数：crypto / serialize 两层的测试要读仓库里的 test-vectors/ 目录，
# 因此需要传入仓库根路径。store / service / ui 的用例自包含，不需要参数。
$suites = @(
    @{ Name = "crypto";     Exe = "crypto_test.exe";    Args = @($Root); Vector = $true },
    @{ Name = "serialize";  Exe = "serialize_test.exe"; Args = @($Root); Vector = $true },
    @{ Name = "store";      Exe = "store_test.exe";     Args = @();      Vector = $false },
    @{ Name = "service";    Exe = "service_test.exe";   Args = @();      Vector = $false },
    @{ Name = "ui";         Exe = "ui_test.exe";        Args = @();      Vector = $false }
)

$failed = @()
$started = Get-Date

Write-Host "=== 测试环境 ==="
Write-Host "架构      : $Architecture"
Write-Host "构建目录  : $buildDir"
Write-Host "仓库根    : $((Resolve-Path $Root).Path)"
Write-Host ""

foreach ($suite in $suites) {
    $exe = Join-Path $buildDir $suite.Exe
    if (-not (Test-Path $exe)) {
        Write-Host "[缺失] $($suite.Name)：未找到 $exe" -ForegroundColor Red
        $failed += $suite.Name
        continue
    }

    $label = "$($suite.Name) ($($suite.Exe))"
    $sw = [Diagnostics.Stopwatch]::StartNew()

    # 测试输出必须纯 ASCII：CI 控制台代码页不是 UTF-8，中文会让 job 变红。
    $output = & $exe @($suite.Args) 2>&1
    $code = $LASTEXITCODE
    $sw.Stop()

    if ($code -eq 0) {
        Write-Host ("[通过] {0}  用时 {1:N1}s" -f $label, $sw.Elapsed.TotalSeconds)
        # 通过时只回显摘要行（末行），完整输出留在失败时打印。
        $summary = $output | Select-Object -Last 1
        if ($summary) { Write-Host "        $summary" }
    } else {
        Write-Host ("[失败] {0}  退出码 {1}  用时 {2:N1}s" -f $label, $code, $sw.Elapsed.TotalSeconds) -ForegroundColor Red
        $output | ForEach-Object { Write-Host "        $_" }
        $failed += $suite.Name
    }
}

$elapsed = (Get-Date) - $started
Write-Host ""
Write-Host "=== 汇总 ==="
Write-Host "耗时 $("{0:N1}" -f $elapsed.TotalSeconds)s"

if ($failed.Count -eq 0) {
    Write-Host "全部通过（$($suites.Count) 层）。" -ForegroundColor Green
    exit 0
}

Write-Host "失败层：$($failed -join '、')" -ForegroundColor Red
exit 1
