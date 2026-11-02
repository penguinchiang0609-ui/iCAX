param([string]$OutputDirectory = [Environment]::GetFolderPath('Desktop'))
$ErrorActionPreference = 'Stop'
# Read-only TPM2_GetCapability. Never Clear, ChangeEPS, provision or write NV.
$report = [ordered]@{ check = 'Intel PTT certificate recovery eligibility'; version = 1; collectedAt = (Get-Date -Format o) }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$report.elevated = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$identity.Dispose()
try {
    if (-not ('TubeDesignerPttRecoveryReadOnly' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TubeDesignerPttRecoveryReadOnly {
    [StructLayout(LayoutKind.Sequential)] struct Parameters { public uint version, flags; }
    [DllImport("tbs.dll")] static extern uint Tbsi_Context_Create(ref Parameters p, out IntPtr context);
    [DllImport("tbs.dll")] static extern uint Tbsip_Context_Close(IntPtr context);
    [DllImport("tbs.dll")] static extern uint Tbsip_Submit_Command(IntPtr context, uint locality, uint priority, byte[] command, uint commandSize, byte[] response, ref uint responseSize);
    static uint U32(byte[] b, int at) { return ((uint)b[at]<<24)|((uint)b[at+1]<<16)|((uint)b[at+2]<<8)|b[at+3]; }
    public static uint Property(uint property) {
        var p = new Parameters { version = 2, flags = 4 }; IntPtr context;
        uint status = Tbsi_Context_Create(ref p, out context);
        if (status != 0) throw new Exception("TBS context: 0x" + status.ToString("X8"));
        try {
            byte[] cmd = { 0x80,1,0,0,0,22,0,0,1,0x7a,0,0,0,6,
                (byte)(property>>24),(byte)(property>>16),(byte)(property>>8),(byte)property,0,0,0,1 };
            var response = new byte[1024]; uint size = (uint)response.Length;
            status = Tbsip_Submit_Command(context, 0, 200, cmd, (uint)cmd.Length, response, ref size);
            if (status != 0) throw new Exception("TBS submit: 0x" + status.ToString("X8"));
            if (size < 10 || size > response.Length || U32(response,2) != size) throw new Exception("Invalid TPM response size");
            var rc = U32(response,6);
            if (rc != 0) throw new Exception("TPM GetCapability: 0x" + rc.ToString("X8"));
            if (size != 27 || response[0] != 0x80 || response[1] != 1 || U32(response,11) != 6 || U32(response,15) != 1 || U32(response,19) != property)
                throw new Exception("Unexpected TPM property response");
            return U32(response,23);
        } finally { Tbsip_Context_Close(context); }
    }
}
'@
    }
    $manufacturer = [TubeDesignerPttRecoveryReadOnly]::Property(0x105)
    $permanent = [TubeDesignerPttRecoveryReadOnly]::Property(0x200)
    $report.manufacturer = ('0x{0:X8}' -f $manufacturer)
    $report.permanentFlags = ('0x{0:X8}' -f $permanent)
    # TPMA_PERMANENT_TPMGENERATEDEPS is 0x00000400 (0x200 is INLOCKOUT).
    $report.tpmGeneratedEPS = (($permanent -band 0x400) -ne 0)
    $report.intelRecoveryBranchCandidate = ($manufacturer -eq 0x494E5443 -and $report.tpmGeneratedEPS)
    $report.interpretation = if ($manufacturer -ne 0x494E5443) {
        'Active TPM is not Intel; the Intel PTT recovery tool does not apply.'
    } elseif ($report.tpmGeneratedEPS) {
        'Intel recovery script selects its iCLS recertification branch for this flag. Firmware/platform/backend support still must be verified; this flag alone is not proof that recovery will work.'
    } else {
        'Intel recovery script uses its normal ekop download branch for this flag. That is the same endpoint already checked by diagnostic v2.'
    }
} catch { $report.tpmError = $_.Exception.Message }
try {
    $report.board = @(Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer,Product,Version)
    $report.bios = @(Get-CimInstance Win32_BIOS | Select-Object SMBIOSBIOSVersion,ReleaseDate)
    $report.tpmFirmware = @(Get-CimInstance -Namespace 'root/CIMV2/Security/MicrosoftTpm' -ClassName Win32_Tpm | Select-Object ManufacturerVersion,ManufacturerVersionInfo)
} catch { $report.systemQueryError = $_.Exception.Message }
$report.intelServices = @()
try {
    $report.intelServices = @(Get-CimInstance Win32_Service -Filter "Name LIKE '%TPM%' OR Name LIKE '%PTT%' OR Name LIKE '%iCLS%'" | Select-Object Name,DisplayName,State)
} catch { $report.serviceQueryError = $_.Exception.Message }
$report.authorizationChanged = $false
$report.recoveryExecuted = $false
if (-not $OutputDirectory -or -not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'Output directory must already exist.' }
$path = Join-Path $OutputDirectory ('PTT-Recovery-State-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding UTF8
Write-Host ('tpmGeneratedEPS: ' + $report.tpmGeneratedEPS)
Write-Host ('Report: ' + $path)
Write-Host 'Read-only check completed. No recovery, firmware update, TPM clear or license change was performed.'
if (-not $report.elevated) { Write-Warning 'Some Windows details may require running Run-Check.cmd as administrator.' }
