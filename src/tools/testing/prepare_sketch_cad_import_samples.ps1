param(
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../../../output/tests/sketch-cad-import/external'),
    [string]$Proxy
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$metadataPath = Join-Path $sourceRoot 'tests/iCAX-UI/fixtures/cad-import/libredwg-samples.json'
$samples = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$targetRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($targetRoot) | Out-Null
foreach ($sample in $samples) {
    if ($sample.id -notmatch '^\d{4}-(Ellipse|Line|circle)$' -or $sample.sha256 -notmatch '^[0-9a-f]{64}$' -or
        -not $sample.url.StartsWith('https://raw.githubusercontent.com/LibreDWG/libredwg/a3d51f1/test/test-data/')) {
        throw 'Invalid pinned DWG sample metadata'
    }
    $target = Join-Path $targetRoot ($sample.id + '.dwg')
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        $download = @{ Uri = $sample.url; OutFile = $target; Headers = @{ 'User-Agent' = 'iCAX-CAD-import-tests' } }
        if ($Proxy) { $download.Proxy = $Proxy }
        Invoke-WebRequest @download
    }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $sample.sha256) {
        throw "DWG sample hash mismatch: $target"
    }
}
Write-Output "Validated $($samples.Count) pinned upstream DWG samples: $targetRoot"
