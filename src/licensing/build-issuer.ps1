#requires -Version 7.0
param(
    [string]$OutputDirectory = '',
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repository = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repository 'output\tools' }
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (-not [IO.Path]::IsPathFullyQualified($OutputDirectory)) {
    $outputRoot = [IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))
}
$buildDirectory = Join-Path $repository 'Temp\licensing-native-issuer-build'
& cmake -S $PSScriptRoot -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native issuer configuration failed' }
$targets = @('td-license-issuer')
if (-not $SkipTests) { $targets += @('td-license-issuer-tests', 'td-license-tests', 'td-license-upgrade-tests', 'td-license-runtime-mode-tests', 'td-license-ambiguous-mode-tests', 'td-license-bundle-tests') }
& cmake --build $buildDirectory --config Release --target $targets --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native issuer build failed' }
if (-not $SkipTests) {
    & ctest --test-dir $buildDirectory -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native issuer verification failed' }
}
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$source = Join-Path $buildDirectory 'Release\iCAX-License-Issuer.exe'
$target = Join-Path $outputRoot 'iCAX-License-Issuer.exe'
if ($source.Equals($target, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The delivery directory must differ from the build directory.'
}
Copy-Item -LiteralPath $source -Destination $target -Force
if ((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
    throw 'Delivered native issuer does not match the verified build.'
}
Write-Output $target
