# 在开发机上编译并运行可移植核心层的测试。
# 核心层不依赖 libnx / SDL，因此可以直接用 MSVC 编译，用来验证解析逻辑。
#
# 用法：pwsh SwitchPort/tests/build_host_test.ps1

$ErrorActionPreference = 'Stop'

$testDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir   = Join-Path (Split-Path -Parent $testDir) 'source'
$coreDir  = Join-Path $srcDir 'core'
$netDir   = Join-Path $srcDir 'net'
$audioDir = Join-Path $srcDir 'audio'
$outDir   = Join-Path $testDir 'build'

$vswhere = Join-Path (Get-Item 'Env:ProgramFiles(x86)').Value 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "找不到 vswhere.exe，请先安装 Visual Studio 或 Build Tools" }
$vsPath = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "找不到 vcvars64.bat：$vcvars" }

if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }

# 注意：这里只收平台无关的源文件。
#   - HttpClient.cpp 依赖 libnx + libcurl
#   - AudioEngine.cpp / FlacDecoder.cpp 依赖 SDL2 + libFLAC
# 它们只能在 Switch 上编译。测试通过 IHttpClient 接口注入假实现覆盖下载流程；
# 音频侧则把最容易出错的环形缓冲区单独抽出来测。
$sources = @(
    (Join-Path $testDir 'host_test.cpp')
    (Join-Path $testDir 'net_test.cpp')
    (Join-Path $testDir 'audio_test.cpp')
(Join-Path $testDir 'tag_test.cpp')
    (Join-Path $testDir 'TestFramework.cpp')
    (Join-Path $audioDir 'AudioRingBuffer.cpp')
    (Join-Path $coreDir 'StringUtil.cpp')
    (Join-Path $coreDir 'FileUtil.cpp')
    (Join-Path $coreDir 'SongInfo.cpp')
    (Join-Path $coreDir 'PathMapper.cpp')
    (Join-Path $coreDir 'PlaylistFile.cpp')
    (Join-Path $coreDir 'LrcParser.cpp')
    (Join-Path $coreDir 'MediaScanner.cpp')
(Join-Path $coreDir 'AudioTag.cpp')
    (Join-Path $coreDir 'Config.cpp')
(Join-Path $coreDir 'VersionUtil.cpp')
    (Join-Path $netDir  'Json.cpp')
(Join-Path $netDir  'ReleaseInfo.cpp')
    (Join-Path $netDir  'UrlUtil.cpp')
    (Join-Path $netDir  'SongMatcher.cpp')
    (Join-Path $netDir  'LyricProvider.cpp')
    (Join-Path $netDir  'DownloadManager.cpp')
) | ForEach-Object { "`"$_`"" }

# /utf-8 让源码里的 u8"" 字面量按 UTF-8 处理；/EHsc 开启标准异常模型
# /I 显式给出核心层目录：多个源文件编译到同一个 /Fo 目录时不能只依赖“相对包含文件所在目录”的查找
$clArgs = "/nologo /std:c++17 /utf-8 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS /I`"$coreDir`" " +
          "/Fo`"$outDir\\`" /Fe`"$outDir\host_test.exe`" " + ($sources -join ' ')

# vcvars 内部会去 PATH 里找 vswhere，找不到时会往 stderr 打无害的告警，一并吞掉
$cmd = "call `"$vcvars`" >nul 2>&1 && cl $clArgs"
& cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw "编译失败" }

Push-Location $outDir
try {
    & (Join-Path $outDir 'host_test.exe')
    $testExit = $LASTEXITCODE
}
finally {
    Pop-Location
}
exit $testExit
