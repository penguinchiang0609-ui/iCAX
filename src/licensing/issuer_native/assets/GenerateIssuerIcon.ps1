param([string]$OutputDirectory = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function New-IssuerBitmap([int]$Size) {
    $sample = 4
    $bitmap = [System.Drawing.Bitmap]::new($Size * $sample, $Size * $sample, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.ScaleTransform(($Size * $sample / 256.0), ($Size * $sample / 256.0))
    $baseBrush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#3730A3'))
    $highlightBrush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#4F46E5'))
    $goldBrush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#FBBF24'))
    $shield = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $shield.AddLine(128, 12, 230, 50)
    $shield.AddLine(230, 50, 230, 123)
    $shield.AddBezier(230, 123, 230, 182, 192, 221, 128, 244)
    $shield.AddBezier(128, 244, 64, 221, 26, 182, 26, 123)
    $shield.AddLine(26, 123, 26, 50)
    $shield.CloseFigure()
    $graphics.FillPath($baseBrush, $shield)
    $highlight = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $highlight.AddLine(128, 27, 215, 60)
    $highlight.AddLine(215, 60, 215, 123)
    $highlight.AddBezier(215, 123, 215, 172, 185, 205, 128, 228)
    $highlight.CloseFigure()
    $graphics.FillPath($highlightBrush, $highlight)

    $key = [System.Drawing.Drawing2D.GraphicsPath]::new([System.Drawing.Drawing2D.FillMode]::Alternate)
    $key.AddEllipse(52, 79, 70, 70)
    $key.AddEllipse(73, 100, 28, 28)
    $graphics.FillPath($goldBrush, $key)
    $shaft = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(107, 131), [System.Drawing.PointF]::new(121, 117),
        [System.Drawing.PointF]::new(149, 145), [System.Drawing.PointF]::new(163, 145),
        [System.Drawing.PointF]::new(163, 159), [System.Drawing.PointF]::new(177, 159),
        [System.Drawing.PointF]::new(177, 180), [System.Drawing.PointF]::new(156, 180))
    $graphics.FillPolygon($goldBrush, $shaft)
    $checkPen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, 14)
    $checkPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $checkPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $checkPen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $graphics.DrawLines($checkPen, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(150, 98), [System.Drawing.PointF]::new(165, 113),
        [System.Drawing.PointF]::new(194, 82)))
    $checkPen.Dispose()
    $key.Dispose()
    $highlight.Dispose()
    $shield.Dispose()
    $goldBrush.Dispose()
    $highlightBrush.Dispose()
    $baseBrush.Dispose()
    $graphics.Dispose()

    $result = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $resize = [System.Drawing.Graphics]::FromImage($result)
    $resize.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $resize.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $resize.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $resize.DrawImage($bitmap, [System.Drawing.Rectangle]::new(0, 0, $Size, $Size))
    $resize.Dispose()
    $bitmap.Dispose()
    return $result
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    $bitmap = New-IssuerBitmap $size
    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $images.Add($stream.ToArray())
    if ($size -eq 256) {
        $bitmap.Save((Join-Path $OutputDirectory 'IssuerIcon.preview.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    $stream.Dispose()
    $bitmap.Dispose()
}
$file = [System.IO.File]::Create((Join-Path $OutputDirectory 'IssuerIcon.ico'))
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)
$offset = 6 + (16 * $sizes.Count)
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dimension = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
    $writer.Write([byte]$dimension)
    $writer.Write([byte]$dimension)
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$i].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($bytes in $images) { $writer.Write($bytes) }
$writer.Dispose()

# Verify every embedded image using the same Windows raster decoder used to render it.
$reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead((Join-Path $OutputDirectory 'IssuerIcon.ico')))
if ($reader.ReadUInt16() -ne 0 -or $reader.ReadUInt16() -ne 1 -or $reader.ReadUInt16() -ne $sizes.Count) {
    throw 'Invalid ICO directory'
}
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $width = $reader.ReadByte(); $height = $reader.ReadByte()
    $reader.ReadByte() | Out-Null; $reader.ReadByte() | Out-Null
    $reader.ReadUInt16() | Out-Null; $reader.ReadUInt16() | Out-Null
    $length = $reader.ReadUInt32(); $position = $reader.ReadUInt32()
    $next = $reader.BaseStream.Position
    $reader.BaseStream.Position = $position
    $embedded = [System.IO.MemoryStream]::new($reader.ReadBytes($length), $false)
    $decoded = [System.Drawing.Image]::FromStream($embedded)
    if ($decoded.Width -ne $sizes[$i] -or $decoded.Height -ne $sizes[$i]) { throw 'ICO size mismatch' }
    if (($width -ne ($sizes[$i] % 256)) -or ($height -ne ($sizes[$i] % 256))) { throw 'ICO directory size mismatch' }
    $decoded.Dispose(); $embedded.Dispose()
    $reader.BaseStream.Position = $next
}
$reader.Dispose()
Write-Output ('Created and verified IssuerIcon.ico with sizes: ' + ($sizes -join ', '))
