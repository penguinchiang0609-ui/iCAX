param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory
)

$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Use x64 PowerShell to validate the x64 licensing module.' }
$binary = Join-Path (Resolve-Path -LiteralPath $BuildDirectory).Path 'TubeDesigner.dll'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Licensing module missing: $binary" }
if (-not ('TubeDesignerPackaging.LicenseBuildReader' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace TubeDesignerPackaging {
    public static class LicenseBuildReader {
        [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32", CharSet=CharSet.Ansi, ExactSpelling=true, SetLastError=true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32", SetLastError=true)]
        private static extern bool FreeLibrary(IntPtr module);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr BuildPolicy();
        public static string Read(string path) {
            // Altered search path resolves dependencies beside this exact DLL.
            var module = LoadLibraryExW(path, IntPtr.Zero, 8);
            if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot load licensing module");
            try {
                var address = GetProcAddress(module, "TubeDesignerLicenseBuildPolicy");
                if (address == IntPtr.Zero) throw new InvalidOperationException("Licensing build metadata missing; rebuild the current licensed Release");
                var result = Marshal.GetDelegateForFunctionPointer<BuildPolicy>(address)();
                if (result == IntPtr.Zero) throw new InvalidOperationException("Licensing module returned no build metadata");
                return Marshal.PtrToStringUTF8(result);
            } finally { FreeLibrary(module); }
        }
    }
}
'@
}
function Assert-LicenseBuildPolicy($Policy, $Catalog) {
    [long]$knownFeatures = 0
    foreach ($feature in $Catalog.features) { $knownFeatures = $knownFeatures -bor [long]$feature.bit }
    if ($Policy.product -ne 'icax.tube-designer' -or
        $Policy.developmentBypass -ne $false -or $Policy.authorizationEnforced -ne $true) {
        throw 'Release packaging requires enforced native authorization; bypass builds cannot be distributed.'
    }
    if ($Policy.configured -ne $true -or -not $Policy.issuer -or $Policy.issuer -eq 'unconfigured') {
        throw 'The Release module has no official issuer public key. Import the existing issuer public key and rebuild before packaging.'
    }
    if ($Policy.certificateFormat -ne $Catalog.certificateFormat -or
        $Policy.featureSchemaVersion -ne $Catalog.schemaVersion -or $Policy.knownFeatures -ne $knownFeatures) {
        throw 'The Release licensing feature contract differs from the current catalog. Rebuild before packaging.'
    }
    $compiledFeatures = @($Policy.featureCatalog)
    $sourceFeatures = @($Catalog.features)
    if ($compiledFeatures.Count -ne $sourceFeatures.Count) {
        throw 'The Release licensing catalog is incomplete. Rebuild before packaging.'
    }
    for ($index = 0; $index -lt $sourceFeatures.Count; $index++) {
        $compiled = $compiledFeatures[$index]
        $source = $sourceFeatures[$index]
        if ($compiled.id -cne $source.id -or $compiled.bit -ne $source.bit -or
            $compiled.label -cne $source.label -or [string]$compiled.parent -cne [string]$source.parent) {
            throw 'The Release licensing permissions differ from the issuer catalog. Rebuild before packaging.'
        }
    }
}
$policy = [TubeDesignerPackaging.LicenseBuildReader]::Read($binary) | ConvertFrom-Json
$catalog = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../licensing/features.json') -Raw | ConvertFrom-Json
Assert-LicenseBuildPolicy $policy $catalog
Write-Output "Validated enforced licensing: $($policy.certificateFormat), $($catalog.features.Count) features, issuer $($policy.issuer)"
