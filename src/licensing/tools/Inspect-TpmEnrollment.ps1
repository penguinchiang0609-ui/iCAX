param([string]$OutputDirectory = [Environment]::GetFolderPath('Desktop'))
$ErrorActionPreference = 'Stop'
# Device/installation diagnosis. No private keys, certificates or license contents are exported.
# The adjacent helper sends public EK digests only to the fixed Intel HTTPS service.
$report = [ordered]@{ diagnosticVersion = 2; collectedAt = (Get-Date -Format o) }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$report.elevated = ([Security.Principal.WindowsPrincipal]::new($identity)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$identity.Dispose()
$report.os = [Environment]::OSVersion.VersionString
$report.processBits = [IntPtr]::Size * 8
if (-not $report.elevated) { Write-Warning '当前未以管理员身份运行，部分结果可能无法读取。请右键 Run-Diagnostic.cmd，选择“以管理员身份运行”。' }
Write-Host '正在读取 TPM、主板和系统记录，并核对 Intel 证书服务；通常需要 1-3 分钟。'
try {
    $report.baseboard = @(Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer,Product,Version)
    $report.bios = @(Get-CimInstance Win32_BIOS | ForEach-Object {
        [ordered]@{ manufacturer = $_.Manufacturer; version = $_.SMBIOSBIOSVersion; releasedAt = if ($_.ReleaseDate) { $_.ReleaseDate.ToString('o') } else { $null } }
    })
    $system = Get-CimInstance Win32_ComputerSystem
    $report.computer = $system | Select-Object Manufacturer,Model
    $os = Get-CimInstance Win32_OperatingSystem
    $report.windows = [ordered]@{ caption = $os.Caption; version = $os.Version; build = $os.BuildNumber; lastBoot = $os.LastBootUpTime.ToString('o') }
} catch { $report.hardwareError = $_.Exception.Message }
try {
    $report.managementEngineDriver = @(Get-CimInstance Win32_PnPSignedDriver -Filter "DeviceName LIKE '%Management Engine%'" | Select-Object DeviceName,DriverVersion,DriverDate,Manufacturer)
} catch { $report.managementEngineDriverError = $_.Exception.Message }
try {
    $report.runningApplications = @(Get-Process TubeDesigner -ErrorAction SilentlyContinue | ForEach-Object {
        $exe = $_.Path
        $row = [ordered]@{ pid = $_.Id; path = $exe; started = $_.StartTime.ToString('o') }
        if ($exe) {
            $file = Get-Item -LiteralPath $exe
            $row.productVersion = $file.VersionInfo.ProductVersion
            $row.exeLastWrite = $file.LastWriteTime.ToString('o')
            $appRoot = Split-Path -Parent $exe
            $manifestPath = Join-Path $appRoot 'apps/tube-designer/product.manifest.json'
            if (Test-Path -LiteralPath $manifestPath) { $row.manifestVersion = (Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json).productVersion }
            $aboutPath = Join-Path $appRoot 'apps/tube-designer/webpage/aboutArea.mjs'
            if (Test-Path -LiteralPath $aboutPath) {
                $aboutText = Get-Content -LiteralPath $aboutPath -Raw -Encoding UTF8
                if ($aboutText -match '<dt>版本</dt><dd>([^<]+)</dd>') { $row.aboutFileVersion = $Matches[1] }
            }
        }
        [pscustomobject]$row
    })
} catch { $report.applicationError = $_.Exception.Message }
try {
    $uninstall = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\iCAX.TubeDesigner' -ErrorAction Stop
    $report.installedApplication = $uninstall | Select-Object DisplayVersion, InstallLocation
} catch { $report.installedApplication = @{ error = $_.Exception.Message } }
try {
    $tpm = Get-Tpm
    if (-not $tpm -or -not $tpm.PSObject.Properties['TpmPresent']) { throw 'Get-Tpm returned no status object. An elevated query may be required; see native query results below.' }
    $report.tpm = $tpm | Select-Object TpmPresent,TpmReady,TpmEnabled,TpmActivated,TpmOwned,RestartPending,AutoProvisioning,ManufacturerIdTxt,ManufacturerVersion,LockedOut
    $report.tpm | Add-Member -NotePropertyName AutoProvisioningText -NotePropertyValue ([string]$tpm.AutoProvisioning)
    $report.tpm | Add-Member -NotePropertyName AutoProvisioningNumeric -NotePropertyValue ([int]$tpm.AutoProvisioning)
} catch { $report.tpm = @{ error = $_.Exception.Message } }
try {
    $report.tpmFirmware = @(Get-CimInstance -Namespace 'root/CIMV2/Security/MicrosoftTpm' -ClassName Win32_Tpm | Select-Object ManufacturerId,ManufacturerVersion,ManufacturerVersionInfo,SpecVersion,PhysicalPresenceVersionInfo)
} catch { $report.tpmFirmwareError = $_.Exception.Message }
try {
    $ek = Get-TpmEndorsementKeyInfo -HashAlgorithm SHA256
    if (-not $ek -or -not $ek.PSObject.Properties['IsPresent']) { throw 'Get-TpmEndorsementKeyInfo returned no certificate status object. See native certificate-store query results below.' }
    $report.windowsEndorsement = @{ isPresent = $ek.IsPresent; manufacturerCertificateCount = @($ek.ManufacturerCertificates | Where-Object { $null -ne $_ }).Count; additionalCertificateCount = @($ek.AdditionalCertificates | Where-Object { $null -ne $_ }).Count }
} catch { $report.windowsEndorsement = @{ error = $_.Exception.Message } }
try {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class TubeDesignerEnrollmentReadOnly {
    [DllImport("ncrypt.dll", CharSet=CharSet.Unicode)] static extern int NCryptOpenStorageProvider(out IntPtr h, string name, uint flags);
    [DllImport("ncrypt.dll", CharSet=CharSet.Unicode)] static extern int NCryptGetProperty(IntPtr h, string name, out IntPtr value, uint bytes, out uint size, uint flags);
    [DllImport("ncrypt.dll")] static extern int NCryptFreeObject(IntPtr h);
    [DllImport("crypt32.dll", SetLastError=true)] static extern IntPtr CertEnumCertificatesInStore(IntPtr store, IntPtr previous);
    [DllImport("crypt32.dll")] static extern bool CertFreeCertificateContext(IntPtr cert);
    [DllImport("crypt32.dll")] static extern bool CertCloseStore(IntPtr store, uint flags);
    [StructLayout(LayoutKind.Sequential)] struct TbsParameters { public uint version, flags; }
    [DllImport("tbs.dll")] static extern uint Tbsi_Context_Create(ref TbsParameters p, out IntPtr context);
    [DllImport("tbs.dll")] static extern uint Tbsip_Context_Close(IntPtr context);
    [DllImport("tbs.dll")] static extern uint Tbsip_Submit_Command(IntPtr context, uint locality, uint priority, byte[] command, uint commandSize, byte[] response, ref uint responseSize);
    static string Hex(uint v) { return "0x" + v.ToString("X8"); }
    public static object Stores() {
        IntPtr provider; int status = NCryptOpenStorageProvider(out provider, "Microsoft Platform Crypto Provider", 0);
        if (status != 0) return new { providerStatus = Hex(unchecked((uint)status)) };
        var stores = new List<object>();
        try {
            foreach (var name in new[] { "PCP_RSA_EKCERT", "PCP_RSA_EKNVCERT", "PCP_EKCERT", "PCP_ECC_EKCERT", "PCP_ECC_EKNVCERT" }) {
                IntPtr store; uint size; status = NCryptGetProperty(provider, name, out store, (uint)IntPtr.Size, out size, 0);
                int count = 0; uint enumerationError = 0;
                if (status == 0 && size == IntPtr.Size && store != IntPtr.Zero) {
                    try {
                        IntPtr cert = IntPtr.Zero;
                        while ((cert = CertEnumCertificatesInStore(store, cert)) != IntPtr.Zero) {
                            if (++count == 128) { CertFreeCertificateContext(cert); break; }
                        }
                        if (count < 128) enumerationError = unchecked((uint)Marshal.GetLastWin32Error());
                    } finally { CertCloseStore(store, 0); }
                }
                stores.Add(new { property = name, status = Hex(unchecked((uint)status)), returnedBytes = size, certificateCount = count, enumerationEnd = Hex(enumerationError) });
            }
        } finally { NCryptFreeObject(provider); }
        return stores;
    }
    public static object NvPublic() {
        // Only TPM2_NV_ReadPublic (0x169). No NV contents, writes, keys or ownership operations.
        var p = new TbsParameters { version = 2, flags = 4 }; IntPtr context;
        uint status = Tbsi_Context_Create(ref p, out context);
        if (status != 0) return new { contextStatus = Hex(status) };
        var results = new List<object>();
        try {
            foreach (uint index in new uint[] { 0x01c00002, 0x01c0000a, 0x01c00100 }) {
                byte[] command = { 0x80,1,0,0,0,14,0,0,1,0x69, (byte)(index>>24),(byte)(index>>16),(byte)(index>>8),(byte)index };
                var reply = new byte[1024]; uint size = (uint)reply.Length;
                status = Tbsip_Submit_Command(context, 0, 200, command, (uint)command.Length, reply, ref size);
                string rc = null; if (status == 0 && size >= 10 && size <= reply.Length) rc = Hex(((uint)reply[6]<<24)|((uint)reply[7]<<16)|((uint)reply[8]<<8)|reply[9]);
                results.Add(new { index = Hex(index), tbsStatus = Hex(status), tpmStatus = rc });
            }
        } finally { Tbsip_Context_Close(context); }
        return results;
    }
}
'@
    $report.certificateStores = [TubeDesignerEnrollmentReadOnly]::Stores()
    $report.certificateNvPublic = [TubeDesignerEnrollmentReadOnly]::NvPublic()
} catch { $report.nativeDiagnosticError = $_.Exception.Message }
try {
    $nativePath = Join-Path $PSScriptRoot 'td-license-enrollment-diagnostics.exe'
    if (-not (Test-Path -LiteralPath $nativePath -PathType Leaf)) { throw 'Missing adjacent diagnostic executable. Extract all files from the ZIP first.' }
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $nativePath
    $start.Arguments = '--collect'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Could not start native diagnostic.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit(150000)
        if ($timedOut) { $process.Kill(); $process.WaitForExit() }
        $fields = [ordered]@{}
        foreach ($line in ($stdoutTask.Result -split "`r?`n")) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) { $fields[$line.Substring(0, $separator)] = $line.Substring($separator + 1) }
        }
        $stderr = $stderrTask.Result
        if ($stderr.Length -gt 4096) { $stderr = $stderr.Substring(0, 4096) }
        $report.nativeEnrollment = [ordered]@{ exitCode = $process.ExitCode; timedOut = $timedOut; fields = $fields; stderr = $stderr }
    } finally { $process.Dispose() }
} catch { $report.nativeEnrollment = @{ error = $_.Exception.Message } }
$report.tpmEvents = @()
$report.eventQueryNotes = @()
try {
    $eventLogs = @(Get-WinEvent -ListLog '*TPM*' -ErrorAction Stop | Where-Object { $_.IsEnabled } | Select-Object -First 4 -ExpandProperty LogName)
} catch { $eventLogs = @(); $report.eventQueryNotes += $_.Exception.Message }
foreach ($eventLog in (@('System') + $eventLogs)) {
    try {
        $filter = @{ LogName = $eventLog; StartTime = (Get-Date).AddDays(-30) }
        if ($eventLog -eq 'System') { $filter.ProviderName = @('TPM','Microsoft-Windows-TPM-WMI') }
        $report.tpmEvents += @(Get-WinEvent -FilterHashtable $filter -MaxEvents 12 -ErrorAction Stop | ForEach-Object {
            $message = [string]$_.Message
            if ($message.Length -gt 2500) { $message = $message.Substring(0, 2500) }
            [ordered]@{ log = $eventLog; at = $_.TimeCreated.ToString('o'); id = $_.Id; level = $_.LevelDisplayName; provider = $_.ProviderName; message = $message }
        })
    } catch { $report.eventQueryNotes += ($eventLog + ': ' + $_.Exception.Message) }
}
$licensePath = Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) 'TubeDesigner/Licensing/license.tdlic'
$report.licenseFile = @{ exists = [IO.File]::Exists($licensePath) }
if ($report.licenseFile.exists) {
    try { $stream = [IO.File]::OpenRead($licensePath); $stream.Dispose(); $report.licenseFile.readable = $true }
    catch { $report.licenseFile.readable = $false; $report.licenseFile.error = $_.Exception.Message }
}
if (-not $OutputDirectory -or -not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'Choose an existing writable output directory.' }
$reportPath = Join-Path $OutputDirectory ('TubeDesigner-TPM-Diagnostic-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.txt')
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "诊断完成，报告：$reportPath"
Write-Host '请把这个报告文件发回。工具未清除 TPM、修改 BIOS 或改动授权文件。'
if (-not $report.elevated) { Write-Warning '此报告权限不足。建议以管理员身份再运行一次，发送新报告。' }
