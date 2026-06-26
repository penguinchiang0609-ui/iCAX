# iCAX 项目编码批量转换: GBK → UTF-8 with BOM
# 用法: 在项目根目录右键 → "使用 PowerShell 运行"，或在终端执行:
#   powershell -ExecutionPolicy Bypass -File convert_encoding.ps1

$ErrorActionPreference = "Stop"
$RootDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Utf8Bom = [System.Text.UTF8Encoding]::new($true)
$GBK = [System.Text.Encoding]::GetEncoding(936)

# 排除目录
$ExcludeDirs = @("packages", "x64", "Debug", "Release", ".vs", "ipch", ".git")

# 文本文件扩展名
$TextExts = @("*.h", "*.hpp", "*.cpp", "*.c", "*.inl", "*.cs",
              "*.fbs", "*.bat", "*.xml", "*.txt", "*.md",
              "*.vcxproj", "*.vcxproj.filters", "*.sln", "*.user",
              "*.natvis", "*.json", "*.cmake", "*.py")

$Stats = @{ Total=0; GBK=0; UTF8noBOM=0; AlreadyBOM=0; ASCII=0; Error=0 }

Write-Host "=== iCAX 编码转换: GBK → UTF-8 BOM ===" -ForegroundColor Cyan
Write-Host "根目录: $RootDir`n"

# 递归收集文件
$Files = @()
foreach ($ext in $TextExts) {
    $found = Get-ChildItem -Path $RootDir -Recurse -Filter $ext -ErrorAction SilentlyContinue |
        Where-Object {
            $full = $_.FullName
            foreach ($ex in $ExcludeDirs) {
                if ($full -match "\$ex\" -or $full -match "\$ex`$") { return $false }
            }
            return $true
        }
    $Files += $found
}

Write-Host "找到 $($Files.Count) 个待检查文件`n"

foreach ($f in $Files) {
    $Stats.Total++
    $path = $f.FullName
    $relPath = $path.Substring($RootDir.Length + 1)
    
    try {
        $bytes = [System.IO.File]::ReadAllBytes($path)
        if ($bytes.Length -eq 0) { $Stats.ASCII++; continue }

        # 检查 UTF-8 BOM (EF BB BF)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            $Stats.AlreadyBOM++
            continue
        }

        # 尝试 UTF-8 解码
        $utf8Str = [System.Text.Encoding]::UTF8.GetString($bytes)
        $hasReplacementChar = $utf8Str.Contains([char]0xFFFD)

        if ($hasReplacementChar) {
            # GBK 编码文件 → 用 CP936 读取，UTF-8 BOM 写入
            $gbkStr = $GBK.GetString($bytes)
            [System.IO.File]::WriteAllText($path, $gbkStr, $Utf8Bom)
            $Stats.GBK++
            Write-Host "  [GBK→UTF8] $relPath" -ForegroundColor Yellow
        }
        elseif ($utf8Str -match '[\u4E00-\u9FFF\u3400-\u4DBF]') {
            # 已有中文的 UTF-8 无 BOM → 仅添加 BOM
            [System.IO.File]::WriteAllText($path, $utf8Str, $Utf8Bom)
            $Stats.UTF8noBOM++
            Write-Host "  [+BOM    ] $relPath" -ForegroundColor Green
        }
        else {
            # 纯 ASCII → 添加 BOM（无害）
            [System.IO.File]::WriteAllText($path, $utf8Str, $Utf8Bom)
            $Stats.ASCII++
        }
    }
    catch {
        $Stats.Error++
        Write-Host "  [ERROR   ] $relPath : $_" -ForegroundColor Red
    }
}

Write-Host "`n=== 转换完成 ===" -ForegroundColor Cyan
Write-Host "  总文件数:     $($Stats.Total)"
Write-Host "  GBK→UTF8:     $($Stats.GBK) (含中文, 重新解码)"
Write-Host "  UTF8+BOM:     $($Stats.UTF8noBOM) (已有中文, 仅加BOM)"
Write-Host "  已是BOM:      $($Stats.AlreadyBOM) (跳过)"
Write-Host "  ASCII+BOM:    $($Stats.ASCII) (纯ASCII, 加BOM)"
Write-Host "  错误:         $($Stats.Error)" -ForegroundColor $(if($Stats.Error -gt 0){'Red'}else{'White'})
Write-Host "`n按任意键退出..." 
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
