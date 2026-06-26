param(
  [int]$Port = 4173,
  [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$HostName = "127.0.0.1"

$MimeTypes = @{
  ".html" = "text/html; charset=utf-8"
  ".css" = "text/css; charset=utf-8"
  ".js" = "application/javascript; charset=utf-8"
  ".mjs" = "application/javascript; charset=utf-8"
  ".json" = "application/json; charset=utf-8"
  ".svg" = "image/svg+xml"
  ".png" = "image/png"
  ".jpg" = "image/jpeg"
  ".jpeg" = "image/jpeg"
  ".ico" = "image/x-icon"
  ".wasm" = "application/wasm"
}

function Resolve-WorkbenchPath {
  param([string]$RequestPath)

  $decoded = [System.Uri]::UnescapeDataString($RequestPath)
  if ($decoded -eq "/") {
    $decoded = "/index.html"
  }

  $relative = $decoded.TrimStart("/").Replace("/", [System.IO.Path]::DirectorySeparatorChar)
  $fullPath = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Root, $relative))
  $rootFullPath = [System.IO.Path]::GetFullPath($Root)

  if (-not $fullPath.StartsWith($rootFullPath, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Forbidden"
  }

  return $fullPath
}

function Write-Response {
  param(
    [System.Net.Sockets.NetworkStream]$Stream,
    [int]$StatusCode,
    [string]$StatusText,
    [string]$ContentType,
    [byte[]]$Body
  )

  $header = "HTTP/1.1 $StatusCode $StatusText`r`nContent-Type: $ContentType`r`nContent-Length: $($Body.Length)`r`nCache-Control: no-store`r`nConnection: close`r`n`r`n"
  $headerBytes = [System.Text.Encoding]::ASCII.GetBytes($header)
  $Stream.Write($headerBytes, 0, $headerBytes.Length)
  $Stream.Write($Body, 0, $Body.Length)
}

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Parse($HostName), $Port)
$listener.Start()

$url = "http://$HostName`:$Port/"
Write-Host "iCAX Workbench: $url"
Write-Host "Press Ctrl+C to stop."
if (-not $NoBrowser) {
  Start-Process $url
}

while ($true) {
  $client = $listener.AcceptTcpClient()
  try {
    $stream = $client.GetStream()
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::ASCII, $false, 1024, $true)
    $requestLine = $reader.ReadLine()
    if ([string]::IsNullOrWhiteSpace($requestLine)) {
      continue
    }

    do {
      $line = $reader.ReadLine()
    } while (-not [string]::IsNullOrEmpty($line))

    $parts = $requestLine.Split(" ")
    $method = $parts[0]
    $path = $parts[1].Split("?")[0]

    if ($method -ne "GET" -and $method -ne "HEAD") {
      $body = [System.Text.Encoding]::UTF8.GetBytes("Method not allowed")
      Write-Response $stream 405 "Method Not Allowed" "text/plain; charset=utf-8" $body
      continue
    }

    try {
      $filePath = Resolve-WorkbenchPath $path
      if (-not [System.IO.File]::Exists($filePath)) {
        throw "Not found"
      }

      $extension = [System.IO.Path]::GetExtension($filePath).ToLowerInvariant()
      $contentType = if ($MimeTypes.ContainsKey($extension)) { $MimeTypes[$extension] } else { "application/octet-stream" }
      $body = if ($method -eq "HEAD") { [byte[]]::new(0) } else { [System.IO.File]::ReadAllBytes($filePath) }
      Write-Response $stream 200 "OK" $contentType $body
    } catch {
      $body = [System.Text.Encoding]::UTF8.GetBytes("Not found")
      Write-Response $stream 404 "Not Found" "text/plain; charset=utf-8" $body
    }
  } finally {
    $client.Close()
  }
}
