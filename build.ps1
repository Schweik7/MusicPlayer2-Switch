# 从 PowerShell 构建 Switch 版，无需手动打开 devkitPro 的 MSYS2 终端。
#
#   pwsh SwitchPort/build.ps1              # 增量构建
#   pwsh SwitchPort/build.ps1 clean        # 清理
#   pwsh SwitchPort/build.ps1 rebuild      # 清理后重新构建
#
# Makefile 依赖 $DEVKITPRO、devkitPro 版的 make 和一系列 POSIX 工具，
# 所以实际构建必须在 MSYS2 环境里跑，这个脚本只是把它包了一层。

param(
    [ValidateSet('build', 'clean', 'rebuild')]
    [string]$Target = 'build'
)

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# 定位 devkitPro：优先环境变量，其次常见安装位置
$devkitPro = $env:DEVKITPRO
if (-not $devkitPro -or -not (Test-Path $devkitPro)) {
    $devkitPro = @('C:\devkitPro', 'D:\devkitPro') | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $devkitPro) {
    throw "找不到 devkitPro。请安装 devkitPro 或设置 DEVKITPRO 环境变量。"
}

$bash = Join-Path $devkitPro 'msys2\usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    throw "找不到 MSYS2 的 bash：$bash"
}

# 把 Windows 路径转成 MSYS2 的形式：D:\a\b -> /d/a/b
$drive = $projectDir.Substring(0, 1).ToLower()
$rest = $projectDir.Substring(2).Replace('\', '/')
$msysProjectDir = "/$drive$rest"

$makeTarget = switch ($Target) {
    'clean'   { 'clean' }
    'rebuild' { 'clean all' }
    default   { '' }
}

# 单引号 here-string：内容原样传给 bash，不让 PowerShell 展开 $ 变量
$script = @"
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=/opt/devkitpro/devkitA64
export PATH=`$DEVKITPRO/tools/bin:`$DEVKITA64/bin:`$PATH
cd '$msysProjectDir' || exit 1
make $makeTarget
"@

$scriptFile = Join-Path ([System.IO.Path]::GetTempPath()) 'mp2_switch_build.sh'
# MSYS2 的 bash 不接受 CRLF 换行，必须写成 LF
[System.IO.File]::WriteAllText($scriptFile, $script.Replace("`r`n", "`n"))

& $bash -l $scriptFile
$exitCode = $LASTEXITCODE
Remove-Item $scriptFile -ErrorAction SilentlyContinue

if ($exitCode -ne 0) {
    throw "构建失败（exit $exitCode）"
}

$nro = Join-Path $projectDir 'MusicPlayer2.nro'
if (Test-Path $nro) {
    $size = [math]::Round((Get-Item $nro).Length / 1MB, 1)
    Write-Host "`n构建成功：$nro ($size MB)"
    Write-Host "把它复制到 SD 卡的 /switch/ 目录，用 hbmenu 启动。"
}
