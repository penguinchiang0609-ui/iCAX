param(
    [Parameter(Mandatory = $true)] [string]$OutputPath,
    [Parameter(Mandatory = $true)] [string]$StopSignal,
    [int]$FramesPerSecond = 8
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing.Common -ErrorAction SilentlyContinue

$ffmpeg = "C:\Users\pengu\AppData\Local\ms-playwright\ffmpeg-1011\ffmpeg-win64.exe"
if (-not (Test-Path -LiteralPath $ffmpeg -PathType Leaf)) {
    throw "Recording encoder was not found: $ffmpeg"
}

$output = [IO.Path]::GetFullPath($OutputPath)
$signal = [IO.Path]::GetFullPath($StopSignal)
$outputDirectory = Split-Path -Parent $output
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$bounds = [Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = [Drawing.Bitmap]::new($bounds.Width, $bounds.Height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$bytes = [byte[]]::new($bounds.Width * $bounds.Height * 4)

$encoder = [Diagnostics.Process]::new()
$encoder.StartInfo.FileName = $ffmpeg
$encoder.StartInfo.Arguments = @(
    "-hide_banner", "-loglevel", "error", "-y",
    "-f", "rawvideo", "-pixel_format", "bgra",
    "-video_size", "$($bounds.Width)x$($bounds.Height)",
    "-framerate", "$FramesPerSecond", "-i", "pipe:0",
    "-vf", "vflip", "-c:v", "libvpx", "-quality", "realtime",
    "-cpu-used", "8", "-b:v", "3500k", "-auto-alt-ref", "0",
    ('"' + $output + '"')
) -join " "
$encoder.StartInfo.UseShellExecute = $false
$encoder.StartInfo.CreateNoWindow = $true
$encoder.StartInfo.RedirectStandardInput = $true
$encoder.StartInfo.RedirectStandardError = $true
if (-not $encoder.Start()) { throw "Failed to start recording encoder" }

$interval = [TimeSpan]::FromSeconds(1.0 / [Math]::Max(1, $FramesPerSecond))
$clock = [Diagnostics.Stopwatch]::StartNew()
$nextFrame = [TimeSpan]::Zero
try {
    while (-not (Test-Path -LiteralPath $signal) -and -not $encoder.HasExited) {
        $graphics.CopyFromScreen($bounds.Location, [Drawing.Point]::Empty, $bounds.Size)
        $data = $bitmap.LockBits(
            [Drawing.Rectangle]::new(0, 0, $bounds.Width, $bounds.Height),
            [Drawing.Imaging.ImageLockMode]::ReadOnly,
            [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            [Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
        }
        finally {
            $bitmap.UnlockBits($data)
        }
        $encoder.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
        $nextFrame += $interval
        $remaining = $nextFrame - $clock.Elapsed
        if ($remaining.TotalMilliseconds -gt 1) {
            [Threading.Thread]::Sleep([Math]::Floor($remaining.TotalMilliseconds))
        }
    }
}
finally {
    $encoder.StandardInput.Close()
    $encoder.WaitForExit()
    $errorText = $encoder.StandardError.ReadToEnd()
    $graphics.Dispose()
    $bitmap.Dispose()
    $encoder.Dispose()
}
if ($errorText) { throw $errorText }
Write-Output $output
