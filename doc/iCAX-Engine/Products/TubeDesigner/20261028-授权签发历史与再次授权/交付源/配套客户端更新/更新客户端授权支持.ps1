#requires -Version 5.1
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$TubeDesignerDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$expectedOriginalHash = 'B492E44220610247809575692FB5A7733D89F0250D8373957406415BC67E098C'
$expectedUpdatedHash = '138F6D98184FB49E9CBF0BCD30C7FB8B7A2D92B44EDE2A6015CE1E12ABE619D9'

function Get-DllHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}
function Assert-RegularFile([string]$Path) {
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "需要实际文件，不能使用目录或链接：$Path"
    }
}
function Assert-ApplicationClosed([string]$Directory) {
    $prefix = $Directory.TrimEnd([char]92, [char]47) + [IO.Path]::DirectorySeparatorChar
    foreach ($process in Get-CimInstance -ClassName Win32_Process -ErrorAction Stop) {
        if ($process.ExecutablePath) {
            $processPath = [IO.Path]::GetFullPath($process.ExecutablePath)
            if ($processPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "请先关闭该安装目录下的程序：$($process.Name)，进程号 $($process.ProcessId)。"
            }
        } elseif ($process.Name -ieq 'TubeDesigner.exe') {
            throw '发现正在运行的 TubeDesigner，但无法核实它所在的目录。请先关闭 TubeDesigner 再更新。'
        }
    }
}

$resolved = Resolve-Path -LiteralPath $TubeDesignerDirectory
if ($resolved.Provider.Name -ne 'FileSystem') { throw '请选择 TubeDesigner 的实际安装目录。' }
$targetDirectory = $resolved.ProviderPath
$directoryItem = Get-Item -LiteralPath $targetDirectory -Force
if (-not $directoryItem.PSIsContainer -or ($directoryItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
    throw '请选择实际安装目录，不能使用文件或目录链接。'
}
# 本次独立程序的实际入口是 TubeDesigner.exe。
$entryPath = Join-Path $targetDirectory 'TubeDesigner.exe'
$targetDll = Join-Path $targetDirectory 'TubeDesigner.dll'
if (-not (Test-Path -LiteralPath $entryPath -PathType Leaf) -or -not (Test-Path -LiteralPath $targetDll -PathType Leaf)) {
    throw '目录不正确：需要同一目录下的 TubeDesigner.exe 和 TubeDesigner.dll。'
}
Assert-RegularFile $entryPath
Assert-RegularFile $targetDll
$replacementDll = Join-Path $PSScriptRoot 'TubeDesigner.dll'
Assert-RegularFile $replacementDll
if ((Get-DllHash $replacementDll) -ne $expectedUpdatedHash) {
    throw '更新文件校验失败，请重新取得完整的配套客户端更新文件夹。'
}

$pathHasher = [Security.Cryptography.SHA256]::Create()
try {
    $pathHash = [BitConverter]::ToString($pathHasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($targetDirectory.ToUpperInvariant()))).Replace('-', '')
} finally { $pathHasher.Dispose() }
$updateMutex = New-Object Threading.Mutex($false, ('Local\TubeDesigner.AuthorizationUpdate.' + $pathHash))
$held = $false
try {
    try { $held = $updateMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $held = $true }
    if (-not $held) { throw '此目录已有另一个更新程序正在运行，请等它结束。' }
    Assert-ApplicationClosed $targetDirectory
    $beforeHash = Get-DllHash $targetDll
    if ($beforeHash -eq $expectedUpdatedHash) {
        Write-Output '已具备本次授权支持，无需重复更新。'
        return
    }
    if ($beforeHash -ne $expectedOriginalHash) {
        throw "当前版本与本次更新不匹配，未修改任何程序文件。当前 DLL SHA256：$beforeHash"
    }
    $backupRoot = Join-Path $targetDirectory '授权支持更新备份'
    if (Test-Path -LiteralPath $backupRoot) {
        $backupItem = Get-Item -LiteralPath $backupRoot -Force
        if (-not $backupItem.PSIsContainer -or ($backupItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw '备份位置不是实际目录，未开始更新。'
        }
    } else { [void][IO.Directory]::CreateDirectory($backupRoot) }
    $runDirectory = Join-Path $backupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $runDirectory) { throw '本次备份目录已存在，未开始更新。' }
    [void][IO.Directory]::CreateDirectory($runDirectory)
    $backupDll = Join-Path $runDirectory 'TubeDesigner.dll'
    $stagedDll = Join-Path $runDirectory 'TubeDesigner.dll.new'
    [IO.File]::Copy($targetDll, $backupDll, $false)
    if ((Get-DllHash $backupDll) -ne $expectedOriginalHash) { throw '原程序备份校验失败，未开始替换。' }
    [IO.File]::Copy($replacementDll, $stagedDll, $false)
    if ((Get-DllHash $stagedDll) -ne $expectedUpdatedHash) { throw '暂存更新文件校验失败，原程序未修改。' }
    Assert-ApplicationClosed $targetDirectory
    if ((Get-DllHash $targetDll) -ne $expectedOriginalHash) { throw '准备更新时原程序发生变化，已停止替换。' }
    try {
        # 同一磁盘内原子替换；本次备份从不覆盖既有备份。
        [IO.File]::Replace($stagedDll, $targetDll, [NullString]::Value)
        if ((Get-DllHash $targetDll) -ne $expectedUpdatedHash) { throw '替换后的程序校验失败。' }
    } catch {
        $updateFailure = $_.Exception.Message
        try {
            if ((Get-DllHash $backupDll) -ne $expectedOriginalHash) { throw '备份文件校验失败。' }
            if (-not (Test-Path -LiteralPath $targetDll -PathType Leaf) -or (Get-DllHash $targetDll) -ne $expectedOriginalHash) {
                [IO.File]::Copy($backupDll, $targetDll, $true)
            }
            if ((Get-DllHash $targetDll) -ne $expectedOriginalHash) { throw '恢复后的程序校验失败。' }
        } catch {
            throw "更新失败，自动恢复也未完成。请保留备份并联系维护人员。备份：$backupDll；更新原因：$updateFailure；恢复原因：$($_.Exception.Message)"
        }
        throw "更新失败，已恢复原程序。原因：$updateFailure；备份：$backupDll"
    }
    Write-Output "更新完成。旧文件备份：$backupDll"
    Write-Output "新 DLL SHA256：$expectedUpdatedHash"
} finally {
    if ($held) { $updateMutex.ReleaseMutex() }
    $updateMutex.Dispose()
}
