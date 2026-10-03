# SecretKeeper - PowerShell UTF-8 环境
#
# 用法（手动引入，不会自动改写你的 $PROFILE）：
#   . .\scripts\ps-profile.ps1
#
# 引入后本会话的当前进程与后续子进程都按 UTF-8 工作，解决两类问题：
#   1. 原生命令输出中文在 GBK 控制台上显示为乱码；
#   2. Python 的 sys.stdout.encoding 为 gbk，print 中文抛 UnicodeEncodeError。
#
# 注意：CI 脚本内部自行重配编码，不依赖本文件；这里只服务本地开发。

chcp 65001 > $null

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[Console]::InputEncoding  = $utf8NoBom
[Console]::OutputEncoding = $utf8NoBom
$OutputEncoding = $utf8NoBom

$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
