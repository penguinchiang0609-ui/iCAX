param(
    [string]$ApplicationPath = "",
    [string]$WorkingDirectory = "",
    [int]$RemoteDebuggingPort = 9223,
    [switch]$StartApplication,
    [switch]$CreateProject,
    [switch]$OpenProject,
    [string]$ProductId = "icax.laser-3d-cam",
    [string]$ProjectName = "Laser3DCAM UI Smoke",
    [string]$ProjectPath = "",
    [string]$MachineDefinitionPath = "",
    [string]$WorkpiecePath = "",
    [switch]$RecognizeCADIntent,
    [switch]$CheckTubeCSGWorkflow,
    [switch]$CheckTubeDesignerWorkflow,
    [switch]$CheckTubeDesignerProfilePreview,
    [switch]$CheckTubeDesignerStepExport,
    [switch]$CheckDefaultMachine,
    [switch]$CheckMachineEnableWorkflow,
    [switch]$CheckMachineRenameWorkflow,
    [switch]$CheckMachineSelectionWorkflow,
    [switch]$CheckWorkbenchResizeWorkflow,
    [switch]$RequireRenderable,
    [string]$ScreenshotPath = "",
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

if ($WorkpiecePath -and (Test-Path -LiteralPath $WorkpiecePath)) {
    $WorkpiecePath = (Resolve-Path -LiteralPath $WorkpiecePath).Path
}
if ($MachineDefinitionPath -and (Test-Path -LiteralPath $MachineDefinitionPath)) {
    $MachineDefinitionPath = (Resolve-Path -LiteralPath $MachineDefinitionPath).Path
}

function Resolve-DefaultApplicationPath {
    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $central = Join-Path $root "x64\Debug\Application.exe"
    if (Test-Path -LiteralPath $central) {
        return $central
    }
    $projectOutput = Join-Path $root "iCAX-Application\Application\x64\Debug\Application.exe"
    if (Test-Path -LiteralPath $projectOutput) {
        return $projectOutput
    }
    return Join-Path $root "iCAX-Engine\x64\Debug\Application.exe"
}

function New-DefaultProjectPath {
    param([string]$TargetProductId)

    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $dir = Join-Path $root ".codex_tmp\ui-smoke"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
    $isTubeOne = [StringComparer]::OrdinalIgnoreCase.Equals($TargetProductId, "icax.tube-one")
    $isTubeDesigner = [StringComparer]::OrdinalIgnoreCase.Equals($TargetProductId, "icax.tube-designer")
    $prefix = if ($isTubeOne) { "tubeone-ui-smoke" } elseif ($isTubeDesigner) { "tube-designer-ui-smoke" } else { "laser3dcam-ui-smoke" }
    $extension = if ($isTubeOne) { ".tubeone" } elseif ($isTubeDesigner) { ".tubedesigner" } else { ".i3cam" }
    return Join-Path $dir "$prefix-$stamp$extension"
}

function Sync-ApplicationRuntimeDependencies {
    param([string]$ApplicationPath)

    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $appDir = Split-Path $ApplicationPath
    $engineRuntime = Join-Path $root "x64\Debug"
    $runtimeProjects = @(
        "iCAX-Engine\foundation\Data",
        "iCAX-Engine\foundation\Task",
        "iCAX-Engine\foundation\GeometryData",
        "iCAX-Engine\framework\ApplicationContext",
        "iCAX-Engine\framework\ApplicationRuntime",
        "iCAX-Engine\framework\Behaviour",
        "iCAX-Engine\framework\SDO",
        "iCAX-Engine\framework\Database",
        "iCAX-Engine\framework\EntityViewRuntime",
        "iCAX-Engine\framework\PDO",
        "iCAX-Engine\framework\Product",
        "iCAX-Engine\framework\ProductContext",
        "iCAX-Engine\framework\Project",
        "iCAX-Engine\framework\ProjectContext",
        "iCAX-Engine\framework\Resources",
        "iCAX-Engine\framework\Services",
        "iCAX-Plugins\cad\OpenCascadeResourceImport",
        "iCAX-Plugins\cam\Laser3DCAM",
        "iCAX-Plugins\common\Transform",
        "iCAX-Plugins\geometry\ExtrusionRecognition",
        "iCAX-Plugins\geometry\DAEResourceImport",
        "iCAX-Plugins\geometry\STLResourceImport",
        "iCAX-Plugins\physics\ColliderData",
        "iCAX-Plugins\product\TubeDesigner",
        "iCAX-Plugins\render\RenderData",
        "iCAX-Plugins\render\RenderInteraction"
    )

    function Copy-DllIfNeeded {
        param(
            [string]$SourcePath,
            [string]$TargetDirectory
        )

        if (-not (Test-Path -LiteralPath $SourcePath)) {
            return
        }
        if (-not (Test-Path -LiteralPath $TargetDirectory)) {
            New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
        }

        $sourceItem = Get-Item -LiteralPath $SourcePath
        $target = Join-Path $TargetDirectory $sourceItem.Name
        if ([StringComparer]::OrdinalIgnoreCase.Equals($sourceItem.FullName, $target)) {
            return
        }

        $targetItem = Get-Item -LiteralPath $target -ErrorAction SilentlyContinue
        if ($null -eq $targetItem -or
            $targetItem.Length -ne $sourceItem.Length -or
            $targetItem.LastWriteTimeUtc -lt $sourceItem.LastWriteTimeUtc) {
            Copy-Item -LiteralPath $sourceItem.FullName -Destination $TargetDirectory -Force
        }
    }

    function Get-ProjectRuntimeDlls {
        foreach ($project in $runtimeProjects) {
            $name = Split-Path -Path $project -Leaf
            $projectDll = Join-Path $root ("{0}\x64\Debug\{1}.dll" -f $project, $name)
            $sharedDll = Join-Path $root ("x64\Debug\{0}.dll" -f $name)
            $candidates = @()
            if (Test-Path -LiteralPath $projectDll) {
                $candidates += Get-Item -LiteralPath $projectDll
            }
            if (Test-Path -LiteralPath $sharedDll) {
                $candidates += Get-Item -LiteralPath $sharedDll
            }
            if ($candidates.Count -gt 0) {
                $candidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
            }
        }
    }

    function Copy-ProjectRuntimeDlls {
        param([string]$TargetDirectory)

        foreach ($dll in Get-ProjectRuntimeDlls) {
            Copy-DllIfNeeded -SourcePath $dll.FullName -TargetDirectory $TargetDirectory
        }
    }

    function Copy-RuntimeDlls {
        param([string]$TargetDirectory)

        if (-not (Test-Path -LiteralPath $engineRuntime)) {
            return
        }
        if (-not (Test-Path -LiteralPath $TargetDirectory)) {
            New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
        }

        Get-ChildItem -Path $engineRuntime -Filter "*.dll" | ForEach-Object {
            Copy-DllIfNeeded -SourcePath $_.FullName -TargetDirectory $TargetDirectory
        }
    }

    function Add-ModulePath {
        param(
            [System.Collections.Generic.List[string]]$Paths,
            [AllowNull()][object]$Value
        )

        if ($null -eq $Value) {
            return
        }
        if ($Value -is [System.Array]) {
            foreach ($item in $Value) {
                Add-ModulePath -Paths $Paths -Value $item
            }
            return
        }
        $text = [string]$Value
        if ($text.Trim().Length -gt 0) {
            $Paths.Add($text)
        }
    }

    function Get-Laser3DCamModuleDirectories {
        $manifestPath = Join-Path $root "apps\laser-3d-cam\product.manifest.json"
        if (-not (Test-Path -LiteralPath $manifestPath)) {
            return @()
        }

        $manifestDir = Split-Path $manifestPath
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        $modulePaths = [System.Collections.Generic.List[string]]::new()

        if ($manifest.backend.modules) {
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.dependencies
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.components
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.behaviours
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.services
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.sdo
        }

        if ($manifest.backend.resources.handlers) {
            foreach ($handler in @($manifest.backend.resources.handlers)) {
                Add-ModulePath -Paths $modulePaths -Value $handler.module
            }
        }

        $modulePaths |
            ForEach-Object {
                $relative = $_.Replace('${Platform}', 'x64').Replace('${Configuration}', 'Debug')
                $fullPath = [System.IO.Path]::GetFullPath((Join-Path $manifestDir $relative))
                Split-Path $fullPath
            } |
            Sort-Object -Unique
    }

    Copy-RuntimeDlls -TargetDirectory $appDir
    Copy-ProjectRuntimeDlls -TargetDirectory $appDir
    foreach ($moduleDir in Get-Laser3DCamModuleDirectories) {
        Copy-RuntimeDlls -TargetDirectory $moduleDir
        Copy-ProjectRuntimeDlls -TargetDirectory $moduleDir
    }
}

function Assert-ApplicationRuntimeDependencies {
    param([string]$ApplicationPath)

    $appDir = Split-Path $ApplicationPath
    $requiredFiles = @(
        "Application.exe",
        "UIContainer.dll",
        "CefUIContainer.dll",
        "libcef.dll"
    )
    foreach ($fileName in $requiredFiles) {
        $path = Join-Path $appDir $fileName
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Application runtime dependency is missing: $path"
        }
    }
}

function Ensure-UIContainerSmokeConfig {
    param(
        [string]$WorkingDirectory,
        [int]$RemoteDebuggingPort
    )

    $settingDir = Join-Path $WorkingDirectory "Setting"
    New-Item -ItemType Directory -Path $settingDir -Force | Out-Null
    $configPath = Join-Path $settingDir "UIContainer.Setting"
    @(
        "type=cef",
        "modulePath=CefUIContainer.dll",
        "remoteDebuggingPort=$RemoteDebuggingPort",
        "sdoPollIntervalMS=16"
    ) | Set-Content -LiteralPath $configPath -Encoding UTF8
}

function ConvertTo-JsLiteral {
    param([AllowNull()][object]$Value)
    return ($Value | ConvertTo-Json -Depth 32 -Compress)
}

function Wait-ForCefPage {
    param(
        [int]$Port,
        [int]$Timeout
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($Timeout)
    $lastError = $null
    do {
        try {
            $pages = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/json" -TimeoutSec 2
            $page = @($pages) | Where-Object { $_.type -eq "page" -and $_.webSocketDebuggerUrl } | Select-Object -First 1
            if ($page) {
                return $page
            }
            $lastError = "CEF remote debugger returned no page target."
        } catch {
            $lastError = $_.Exception.Message
        }
        Start-Sleep -Milliseconds 300
    } while ([DateTime]::UtcNow -lt $deadline)

    throw "CEF remote debugger is not available on port $Port. Last error: $lastError"
}

function Invoke-CdpCommand {
    param(
        [System.Net.WebSockets.ClientWebSocket]$Socket,
        [int]$Id,
        [string]$Method,
        [hashtable]$Params
    )

    $payload = @{
        id = $Id
        method = $Method
        params = $Params
    } | ConvertTo-Json -Depth 32 -Compress

    $bytes = [System.Text.Encoding]::UTF8.GetBytes($payload)
    $segment = [ArraySegment[byte]]::new($bytes)
    $null = $Socket.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()

    while ($true) {
        $buffer = New-Object byte[] 65536
        $builder = [System.Text.StringBuilder]::new()
        do {
            $receiveSegment = [ArraySegment[byte]]::new($buffer)
            $result = $Socket.ReceiveAsync($receiveSegment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
            if ($result.MessageType -eq [System.Net.WebSockets.WebSocketMessageType]::Close) {
                throw "CEF debugger websocket closed while waiting for $Method."
            }
            [void]$builder.Append([System.Text.Encoding]::UTF8.GetString($buffer, 0, $result.Count))
        } while (-not $result.EndOfMessage)

        $message = $builder.ToString() | ConvertFrom-Json
        if ($message.id -eq $Id) {
            if ($message.error) {
                throw "CDP command $Method failed: $($message.error.message)"
            }
            return $message
        }
    }
}

if (-not $ApplicationPath) {
    $ApplicationPath = Resolve-DefaultApplicationPath
}
$ApplicationPath = (Resolve-Path $ApplicationPath).Path
if (-not $WorkingDirectory) {
    $WorkingDirectory = Split-Path $ApplicationPath
}
$WorkingDirectory = (Resolve-Path $WorkingDirectory).Path
Sync-ApplicationRuntimeDependencies -ApplicationPath $ApplicationPath
Assert-ApplicationRuntimeDependencies -ApplicationPath $ApplicationPath
Ensure-UIContainerSmokeConfig -WorkingDirectory $WorkingDirectory -RemoteDebuggingPort $RemoteDebuggingPort
if (-not $ProjectPath) {
    $ProjectPath = New-DefaultProjectPath -TargetProductId $ProductId
}
$tubeDesignerExportDirectory = Join-Path (Split-Path -Parent $ProjectPath) (([IO.Path]::GetFileNameWithoutExtension($ProjectPath)) + "-step")

$process = $null
if ($StartApplication) {
    $process = Start-Process -FilePath $ApplicationPath -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru
}

try {
    $page = Wait-ForCefPage -Port $RemoteDebuggingPort -Timeout $TimeoutSeconds
    $socket = [System.Net.WebSockets.ClientWebSocket]::new()
    $null = $socket.ConnectAsync([Uri]$page.webSocketDebuggerUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    try {
        $productIdLiteral = ConvertTo-JsLiteral $ProductId
        $projectNameLiteral = ConvertTo-JsLiteral $ProjectName
        $projectPathLiteral = ConvertTo-JsLiteral $ProjectPath
        $machinePathLiteral = ConvertTo-JsLiteral $MachineDefinitionPath
        $workpiecePathLiteral = ConvertTo-JsLiteral $WorkpiecePath
        $tubeDesignerExportDirectoryLiteral = ConvertTo-JsLiteral $tubeDesignerExportDirectory
        $createProjectLiteral = if ($CreateProject -or $MachineDefinitionPath -or $WorkpiecePath) { "true" } else { "false" }
        $openProjectLiteral = if ($OpenProject) { "true" } else { "false" }
        $importMachineLiteral = if ($MachineDefinitionPath) { "true" } else { "false" }
        $importWorkpieceLiteral = if ($WorkpiecePath) { "true" } else { "false" }
        $recognizeCADIntentLiteral = if ($RecognizeCADIntent) { "true" } else { "false" }
        $checkTubeCSGWorkflowLiteral = if ($CheckTubeCSGWorkflow) { "true" } else { "false" }
        $checkTubeDesignerWorkflowLiteral = if ($CheckTubeDesignerWorkflow) { "true" } else { "false" }
        $checkTubeDesignerProfilePreviewLiteral = if ($CheckTubeDesignerProfilePreview) { "true" } else { "false" }
        $checkTubeDesignerStepExportLiteral = if ($CheckTubeDesignerStepExport) { "true" } else { "false" }
        $checkDefaultMachineLiteral = if ($CheckDefaultMachine) { "true" } else { "false" }
        $checkMachineEnableWorkflowLiteral = if ($CheckMachineEnableWorkflow) { "true" } else { "false" }
        $checkMachineRenameWorkflowLiteral = if ($CheckMachineRenameWorkflow) { "true" } else { "false" }
        $checkMachineSelectionWorkflowLiteral = if ($CheckMachineSelectionWorkflow) { "true" } else { "false" }
        $checkWorkbenchResizeWorkflowLiteral = if ($CheckWorkbenchResizeWorkflow) { "true" } else { "false" }
        $requireRenderableLiteral = if ($RequireRenderable) { "true" } else { "false" }
        $timeoutMs = [Math]::Max(1000, $TimeoutSeconds * 1000)

        $expression = @"
(async () => {
  const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const waitUntil = async (predicate, label) => {
    const deadline = Date.now() + $timeoutMs;
    while (Date.now() < deadline) {
      try {
        const value = predicate();
        if (value) {
          return value;
        }
      } catch {
      }
      await delay(100);
    }
    throw new Error("Timed out waiting for " + label);
  };

  await waitUntil(() => window.__icaxAppShell, "AppShell automation");
  await waitUntil(() => {
    const state = window.__icaxAppShell.getState();
    return state?.products?.some((product) => product?.productId === $productIdLiteral) ? state : null;
  }, "target product in AppShell state");
  let createProjectResult = null;
  if ($openProjectLiteral) {
    createProjectResult = await window.__icaxAppShell.openProject($projectPathLiteral, $productIdLiteral);
    await waitUntil(() => window.__icaxLaser3DCAM, "Laser3DCAM automation").catch((error) => {
      const appState = window.__icaxAppShell?.getState?.() ?? null;
      throw new Error(error.message + "; AppShell=" + JSON.stringify(appState));
    });
  } else if ($createProjectLiteral) {
    createProjectResult = await window.__icaxAppShell.createProject({
      productId: $productIdLiteral,
      projectName: $projectNameLiteral,
      projectPath: $projectPathLiteral
    });
    await waitUntil(() => window.__icaxLaser3DCAM, "Laser3DCAM automation").catch((error) => {
      const appState = window.__icaxAppShell?.getState?.() ?? null;
      throw new Error(error.message + "; AppShell=" + JSON.stringify(appState));
    });
  }

  let defaultMachineResult = null;
  let defaultMachineViewport = null;
  if ($checkDefaultMachineLiteral) {
    defaultMachineResult = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getState?.();
      return Number(state?.machineDefinitionCount ?? 0) > 0 && Number(state?.machineInstanceCount ?? 0) > 0 ? state : null;
    }, "default machine definition and instance");
    if ($requireRenderableLiteral && !$importMachineLiteral) {
      defaultMachineViewport = await window.__icaxLaser3DCAM.waitForRenderableViewport({
        timeoutMs: $timeoutMs,
        includeObjects: true
      });
    }
  }

  let importMachineResult = null;
  if ($importMachineLiteral) {
    importMachineResult = await window.__icaxLaser3DCAM.importMachineDefinition($machinePathLiteral);
    if ($requireRenderableLiteral) {
      importMachineResult.viewport = await window.__icaxLaser3DCAM.waitForRenderableViewport({
        timeoutMs: $timeoutMs,
        includeObjects: true
      });
    }
    await delay(500);
  }

  let importWorkpieceResult = null;
  if ($importWorkpieceLiteral) {
    importWorkpieceResult = await window.__icaxLaser3DCAM.importWorkpiece($workpiecePathLiteral);
    await delay(500);
  }

  let tubeDesignerProfilePreviewResult = null;
  if ($checkTubeDesignerProfilePreviewLiteral) {
    if (typeof window.__icaxAppShell?.selectRibbonTab !== "function") {
      throw new Error("AppShell ribbon-tab automation is unavailable.");
    }
    if (typeof window.__icaxLaser3DCAM?.getTubeDesignerProfileLibraryState !== "function") {
      throw new Error("TubeDesigner profile-library automation is unavailable.");
    }
    await window.__icaxAppShell.selectRibbonTab("profiles");
    try {
      await waitUntil(() => {
        const state = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
        const cards = Array.from(document.querySelectorAll(".tube-profile-library-card"));
        return state.activeAreaId === "profiles" && !state.pending && !state.progress
          && state.profiles.length > 0 && cards.length === state.profiles.length
          && cards.every((card) => !card.disabled)
          ? state : null;
      }, "enabled TubeDesigner profile library");
    } catch (error) {
      const state = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
      throw new Error(error.message + "; state=" + JSON.stringify(state)
        + "; progressBackdrops=" + document.querySelectorAll(".cam-progress-backdrop").length
        + "; disabledCards=" + document.querySelectorAll(".tube-profile-library-card:disabled").length);
    }

    const inspectProfileSectionSvg = (profile) => {
      const editorSelector = "[data-tube-profile-library-editor][data-tube-designer-profile-key=\""
        + CSS.escape(profile.selectionKey) + "\"]";
      const editor = document.querySelector(editorSelector);
      const miniature = editor?.querySelector(".tube-profile-library-miniature") ?? null;
      const svg = miniature?.querySelector(".tube-profile-library-svg") ?? null;
      const outer = svg?.querySelector(".outer") ?? null;
      const geometry = svg?.querySelector("g") ?? null;
      if (!svg || !outer || !geometry) {
        return {
          svgExists: Boolean(svg),
          outerExists: Boolean(outer),
          geometryExists: Boolean(geometry),
          fullyContained: false,
          hasSafePadding: false,
          svgElementContained: false,
          svgElementSizedToFrame: false,
          curveExpected: /圆|椭圆|梅花|弧|样条|spline/i.test(profile.name),
          smoothCurveExpression: false,
          periodicBsplineTarget: /(^|[^0-9])06[_\s-]|五瓣梅花/i.test(profile.name),
        };
      }

      const miniatureRect = miniature.getBoundingClientRect();
      const svgRect = svg.getBoundingClientRect();
      const elementTolerance = 0.75;
      const svgElementContained = svgRect.left >= miniatureRect.left - elementTolerance
        && svgRect.top >= miniatureRect.top - elementTolerance
        && svgRect.right <= miniatureRect.right + elementTolerance
        && svgRect.bottom <= miniatureRect.bottom + elementTolerance;
      const svgWidthRatio = svgRect.width / Math.max(1, miniature.clientWidth);
      const svgHeightRatio = svgRect.height / Math.max(1, miniature.clientHeight);
      const svgElementSizedToFrame = svgWidthRatio >= 0.84 && svgWidthRatio <= 0.88
        && svgHeightRatio >= 0.84 && svgHeightRatio <= 0.88;
      const viewBox = svg.viewBox.baseVal;
      const localBounds = geometry.getBBox();
      const matrix = geometry.transform.baseVal.consolidate()?.matrix ?? {
        a: 1, b: 0, c: 0, d: 1, e: 0, f: 0
      };
      const transformPoint = (x, y) => ({
        x: matrix.a * x + matrix.c * y + matrix.e,
        y: matrix.b * x + matrix.d * y + matrix.f,
      });
      const corners = [
        transformPoint(localBounds.x, localBounds.y),
        transformPoint(localBounds.x + localBounds.width, localBounds.y),
        transformPoint(localBounds.x, localBounds.y + localBounds.height),
        transformPoint(localBounds.x + localBounds.width, localBounds.y + localBounds.height),
      ];
      const renderedBounds = {
        minX: Math.min(...corners.map((point) => point.x)),
        minY: Math.min(...corners.map((point) => point.y)),
        maxX: Math.max(...corners.map((point) => point.x)),
        maxY: Math.max(...corners.map((point) => point.y)),
      };
      const margins = {
        left: renderedBounds.minX - viewBox.x,
        top: renderedBounds.minY - viewBox.y,
        right: viewBox.x + viewBox.width - renderedBounds.maxX,
        bottom: viewBox.y + viewBox.height - renderedBounds.maxY,
      };
      const tolerance = Math.max(viewBox.width, viewBox.height) * 0.0001;
      const fullyContained = Object.values(margins).every((margin) => margin >= -tolerance);
      const horizontalPaddingRatio = Math.min(margins.left, margins.right)
        / Math.max(1e-9, viewBox.width);
      const verticalPaddingRatio = Math.min(margins.top, margins.bottom)
        / Math.max(1e-9, viewBox.height);
      const hasSafePadding = fullyContained
        && horizontalPaddingRatio >= 0.01
        && verticalPaddingRatio >= 0.01;

      const outerTag = outer.tagName.toLowerCase();
      const outerPathData = outerTag === "path" ? String(outer.getAttribute("d") ?? "") : "";
      const pathCurveCommandCount = (outerPathData.match(/[aAcCqQsStT]/g) ?? []).length;
      const pathLineCommandCount = (outerPathData.match(/[lL]/g) ?? []).length;
      const primitiveIsSmooth = outerTag === "circle" || outerTag === "ellipse"
        || (outerTag === "rect" && Number(outer.getAttribute("rx") ?? 0) > 0);
      const polygonPointCount = (outerTag === "polygon" || outerTag === "polyline")
        ? Number(outer.points?.numberOfItems ?? 0) : 0;
      const denseLinearApproximation = polygonPointCount >= 32 || pathLineCommandCount >= 32;
      const smoothCurveExpression = primitiveIsSmooth
        || pathCurveCommandCount > 0
        || denseLinearApproximation;
      const curveExpected = /圆|椭圆|梅花|弧|样条|spline/i.test(profile.name);
      const periodicBsplineTarget = /(^|[^0-9])06[_\s-]|五瓣梅花/i.test(profile.name);
      return {
        svgExists: true,
        outerExists: true,
        geometryExists: true,
        outerTag,
        miniatureRect: {
          left: miniatureRect.left,
          top: miniatureRect.top,
          right: miniatureRect.right,
          bottom: miniatureRect.bottom,
          width: miniatureRect.width,
          height: miniatureRect.height,
        },
        svgRect: {
          left: svgRect.left,
          top: svgRect.top,
          right: svgRect.right,
          bottom: svgRect.bottom,
          width: svgRect.width,
          height: svgRect.height,
        },
        svgWidthRatio,
        svgHeightRatio,
        svgElementContained,
        svgElementSizedToFrame,
        viewBox: {
          x: viewBox.x,
          y: viewBox.y,
          width: viewBox.width,
          height: viewBox.height,
        },
        renderedBounds,
        margins,
        horizontalPaddingRatio,
        verticalPaddingRatio,
        fullyContained,
        hasSafePadding,
        curveExpected,
        primitiveIsSmooth,
        pathCurveCommandCount,
        pathLineCommandCount,
        polygonPointCount,
        denseLinearApproximation,
        smoothCurveExpression,
        periodicBsplineTarget,
      };
    };

    const initialState = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
    const systemGroup = document.querySelector('[data-tube-profile-library-group="system"]');
    const userGroup = document.querySelector('[data-tube-profile-library-group="user"]');
    const libraryGroups = {
      groupCount: document.querySelectorAll("[data-tube-profile-library-group]").length,
      systemExists: Boolean(systemGroup),
      userExists: Boolean(userGroup),
      systemCardCount: systemGroup?.querySelectorAll(
        '.tube-profile-library-card[data-tube-designer-profile-scope="system"]'
      ).length ?? 0,
      userCardCount: userGroup?.querySelectorAll(
        '.tube-profile-library-card[data-tube-designer-profile-scope="user"]'
      ).length ?? 0,
    };
    const profileList = document.querySelector(".tube-profile-library-list");
    if (!profileList || !systemGroup || !userGroup) {
      throw new Error("TubeDesigner profile-list layout elements are missing.");
    }
    systemGroup.open = true;
    userGroup.open = true;
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const rectSnapshot = (element) => {
      const rect = element.getBoundingClientRect();
      return {
        left: rect.left,
        top: rect.top,
        right: rect.right,
        bottom: rect.bottom,
        width: rect.width,
        height: rect.height,
      };
    };
    const inspectExpandedGroupLayout = (group) => {
      const content = group.querySelector(":scope > .tube-profile-library-group-content");
      const cards = Array.from(group.querySelectorAll(":scope > .tube-profile-library-group-content > .tube-profile-library-card"));
      const groupRect = rectSnapshot(group);
      const contentRect = content ? rectSnapshot(content) : null;
      const cardRects = cards.map((card) => ({
        key: card.dataset.tubeDesignerProfileKey ?? "",
        ...rectSnapshot(card),
      }));
      const tolerance = 1;
      return {
        open: group.open,
        groupRect,
        contentRect,
        clientHeight: group.clientHeight,
        scrollHeight: group.scrollHeight,
        contentClientHeight: content?.clientHeight ?? 0,
        contentScrollHeight: content?.scrollHeight ?? 0,
        cardRects,
        allCardsHaveHeight: cardRects.length > 0 && cardRects.every((rect) => rect.height > 0),
        groupContainsAllCards: cardRects.every((rect) =>
          rect.top >= groupRect.top - tolerance && rect.bottom <= groupRect.bottom + tolerance),
        contentContainsAllCards: Boolean(contentRect) && cardRects.every((rect) =>
          rect.top >= contentRect.top - tolerance && rect.bottom <= contentRect.bottom + tolerance),
        groupHasNoClippedOverflow: group.scrollHeight <= group.clientHeight + tolerance,
        contentHasNoClippedOverflow: Boolean(content)
          && content.scrollHeight <= content.clientHeight + tolerance,
      };
    };
    const expandedGroupLayout = {
      system: inspectExpandedGroupLayout(systemGroup),
      user: inspectExpandedGroupLayout(userGroup),
    };
    profileList.scrollTop = 0;
    await new Promise((resolve) => requestAnimationFrame(resolve));
    const scrollTopBefore = profileList.scrollTop;
    const lastSystemCard = systemGroup.querySelector(
      ':scope > .tube-profile-library-group-content > .tube-profile-library-card:last-child'
    );
    lastSystemCard?.scrollIntoView({ block: "end", inline: "nearest" });
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const scrollTopAfterSystem = profileList.scrollTop;
    const listRect = rectSnapshot(profileList);
    const listViewportRect = {
      left: listRect.left + profileList.clientLeft,
      top: listRect.top + profileList.clientTop,
      right: listRect.left + profileList.clientLeft + profileList.clientWidth,
      bottom: listRect.top + profileList.clientTop + profileList.clientHeight,
      width: profileList.clientWidth,
      height: profileList.clientHeight,
    };
    const lastSystemCardRect = lastSystemCard ? rectSnapshot(lastSystemCard) : null;
    const visibilityTolerance = 1;
    const lastSystemCardFullyVisible = Boolean(lastSystemCardRect)
      && lastSystemCardRect.left >= listViewportRect.left - visibilityTolerance
      && lastSystemCardRect.top >= listViewportRect.top - visibilityTolerance
      && lastSystemCardRect.right <= listViewportRect.right + visibilityTolerance
      && lastSystemCardRect.bottom <= listViewportRect.bottom + visibilityTolerance;
    const lastUserCard = userGroup.querySelector(
      ':scope > .tube-profile-library-group-content > .tube-profile-library-card:last-child'
    );
    lastUserCard?.scrollIntoView({ block: "end", inline: "nearest" });
    await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)));
    const lastUserCardRect = lastUserCard ? rectSnapshot(lastUserCard) : null;
    const lastUserCardFullyVisible = !lastUserCard || (Boolean(lastUserCardRect)
      && lastUserCardRect.left >= listViewportRect.left - visibilityTolerance
      && lastUserCardRect.top >= listViewportRect.top - visibilityTolerance
      && lastUserCardRect.right <= listViewportRect.right + visibilityTolerance
      && lastUserCardRect.bottom <= listViewportRect.bottom + visibilityTolerance);
    const listLayout = {
      listRect,
      listViewportRect,
      clientHeight: profileList.clientHeight,
      scrollHeight: profileList.scrollHeight,
      scrollTopBefore,
      scrollTopAfter: profileList.scrollTop,
      scrollTopAfterSystem,
      lastSystemCardRect,
      lastUserCardRect,
      hasVerticalOverflow: profileList.scrollHeight > profileList.clientHeight + visibilityTolerance,
      lastSystemCardFullyVisible,
      lastUserCardFullyVisible,
    };
    const invalidExpandedGroups = Object.entries(expandedGroupLayout)
      .filter(([, group]) => !group.open
        || !group.allCardsHaveHeight
        || !group.groupContainsAllCards
        || !group.contentContainsAllCards
        || !group.groupHasNoClippedOverflow
        || !group.contentHasNoClippedOverflow)
      .map(([scope]) => scope);
    if (expandedGroupLayout.system.cardRects.length !== 11
        || invalidExpandedGroups.length
        || !listLayout.hasVerticalOverflow
        || !listLayout.lastSystemCardFullyVisible
        || !listLayout.lastUserCardFullyVisible) {
      throw new Error("TubeDesigner profile-list layout clips cards: " + JSON.stringify({
        invalidExpandedGroups,
        expandedGroupLayout,
        listLayout,
      }));
    }
    const invalidProfileReferences = initialState.profiles.filter((profile) =>
      !profile.id
      || !["system", "user"].includes(profile.scope)
      || profile.selectionKey !== profile.scope + ":" + profile.id);
    if (initialState.systemProfileCount !== 11
        || libraryGroups.groupCount !== 2
        || !libraryGroups.systemExists
        || !libraryGroups.userExists
        || libraryGroups.systemCardCount !== 11
        || libraryGroups.systemCardCount !== initialState.systemProfileCount
        || libraryGroups.userCardCount !== initialState.userProfileCount
        || initialState.profiles.length !== initialState.systemProfileCount + initialState.userProfileCount
        || invalidProfileReferences.length) {
      throw new Error("TubeDesigner profile groups or references are invalid: " + JSON.stringify({
        state: initialState,
        libraryGroups,
        invalidProfileReferences,
      }));
    }
    const progressProbe = document.createElement("div");
    progressProbe.className = "cam-progress-backdrop";
    progressProbe.dataset.uiSmokeProgressProbe = "true";
    document.querySelector(".cam-workbench")?.append(progressProbe);
    await delay(50);
    const progressRect = progressProbe.getBoundingClientRect();
    const progressCoverage = {
      left: Math.round(progressRect.left),
      top: Math.round(progressRect.top),
      right: Math.round(progressRect.right),
      bottom: Math.round(progressRect.bottom),
      viewportWidth: window.innerWidth,
      viewportHeight: window.innerHeight,
      coversViewport: progressRect.left <= 0 && progressRect.top <= 0
        && progressRect.right >= window.innerWidth && progressRect.bottom >= window.innerHeight
    };
    progressProbe.remove();
    const results = [];
    for (const profile of initialState.profiles) {
      const selector = ".tube-profile-library-card[data-tube-designer-profile-key=\""
        + CSS.escape(profile.selectionKey) + "\"]";
      let card = null;
      try {
        card = await waitUntil(() => {
          const candidate = document.querySelector(selector);
          return candidate && !candidate.disabled && !document.querySelector(".cam-progress-backdrop")
            ? candidate : null;
        }, "enabled profile card " + profile.name);
      } catch (error) {
        const state = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
        throw new Error(error.message + "; state=" + JSON.stringify(state)
          + "; cardExists=" + Boolean(document.querySelector(selector))
          + "; cardDisabled=" + Boolean(document.querySelector(selector)?.disabled)
          + "; progressBackdrops=" + document.querySelectorAll(".cam-progress-backdrop").length);
      }
      card.click();
      const startedAt = performance.now();
      const deadline = startedAt + $timeoutMs;
      let current = null;
      let viewport = null;
      let status = "";
      let progressObserved = false;
      let previewWaitObserved = false;
      let previewWaitCoversViewport = false;
      let previewWaitRect = null;
      while (performance.now() < deadline) {
        current = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
        viewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: true, includeObjects: true });
        const statusElement = document.querySelector("[data-tube-profile-preview-status] small");
        const progressElement = document.querySelector("[data-tube-profile-preview-progress]");
        const previewWait = document.querySelector("[data-tube-profile-preview-wait]");
        status = statusElement?.textContent?.trim() ?? "";
        progressObserved ||= Boolean(progressElement && !progressElement.hidden
          && progressElement.getAttribute("aria-hidden") === "false");
        const previewWaitVisible = Boolean(previewWait && !previewWait.hidden
          && previewWait.getAttribute("aria-hidden") === "false");
        if (previewWaitVisible) {
          previewWaitObserved = true;
          const rect = previewWait.getBoundingClientRect();
          previewWaitRect = {
            left: Math.round(rect.left),
            top: Math.round(rect.top),
            right: Math.round(rect.right),
            bottom: Math.round(rect.bottom),
            viewportWidth: window.innerWidth,
            viewportHeight: window.innerHeight,
          };
          previewWaitCoversViewport ||= rect.left <= 0 && rect.top <= 0
            && rect.right >= window.innerWidth && rect.bottom >= window.innerHeight;
        }
        const statusIsError = statusElement?.classList?.contains("error") ?? false;
        if (statusIsError || current.error.startsWith("三维管型预览失败")) {
          throw new Error(profile.name + ": " + (status || current.error));
        }
        let previewIdentityMatches = false;
        try {
          const previewIdentity = JSON.parse(current.previewKey);
          previewIdentityMatches = Array.isArray(previewIdentity)
            && previewIdentity[0] === profile.scope
            && previewIdentity[1] === profile.id;
        } catch {
          previewIdentityMatches = false;
        }
        const expectedRevisionPrefix = "profile-preview:" + profile.selectionKey + ":";
        if (current.selectedProfileKey === profile.selectionKey
            && !current.requestKey
            && previewIdentityMatches
            && current.geometryResourceId
            && current.geometryResourceVersion > 0
            && current.appliedView?.revision?.startsWith(expectedRevisionPrefix)
            && current.appliedView?.entityIds?.length === 1
            && current.appliedView.entityIds[0] === profile.selectionKey
            && status === "三维管型已生成"
            && Number(viewport?.geometryCount ?? 0) >= 1
            && Number(viewport?.objectCount ?? 0) === 1
            && Number(viewport?.visibleObjectCount ?? 0) === 1
            && Number(viewport?.renderInfo?.triangles ?? 0) > 0
            && viewport?.objects?.length === 1
            && viewport.objects[0]?.objectId === profile.selectionKey
            && viewport.objects[0]?.geometryId === current.geometryResourceId
            && Number(viewport?.pixelSample?.nonBackground ?? 0) > 0) {
          break;
        }
        await delay(100);
      }
      if (performance.now() >= deadline) {
        throw new Error(profile.name + ": timed out; state=" + JSON.stringify(current)
          + "; status=" + status + "; viewport=" + JSON.stringify(viewport));
      }
      const bounds = viewport?.contentBounds;
      const spans = bounds ? [
        Number(bounds.max.x) - Number(bounds.min.x),
        Number(bounds.max.y) - Number(bounds.min.y),
        Number(bounds.max.z) - Number(bounds.min.z)
      ] : [];
      if (!spans.some((span) => Math.abs(span - 1000) < 0.01)) {
        throw new Error(profile.name + ": preview is not a 1000 mm extrusion; spans=" + spans.join(","));
      }
      const sectionSvg = inspectProfileSectionSvg(profile);
      results.push({
        id: profile.id,
        scope: profile.scope,
        selectionKey: profile.selectionKey,
        name: profile.name,
        profileType: profile.profileType,
        elapsedMs: Math.round(performance.now() - startedAt),
        status,
        progressObserved,
        previewWaitObserved,
        previewWaitCoversViewport,
        previewWaitRect,
        geometryResourceVersion: current.geometryResourceVersion,
        appliedRevision: current.appliedView.revision,
        appliedEntityId: current.appliedView.entityIds[0],
        viewportObjectId: viewport.objects[0].objectId,
        spans,
        visibleObjectCount: viewport.visibleObjectCount,
        nonBackgroundPixels: viewport.pixelSample.nonBackground,
        sectionSvg,
      });
    }
    const logMessages = Array.from(document.querySelectorAll(".log-row > span:last-child"))
      .map((element) => element.textContent?.trim() ?? "");
    const missingStartLogs = initialState.profiles
      .filter((profile) => !logMessages.some((message) => message === "开始生成三维管型：" + profile.name))
      .map((profile) => profile.name);
    const missingSuccessLogs = initialState.profiles
      .filter((profile) => !logMessages.some((message) => message === "三维管型已生成：" + profile.name))
      .map((profile) => profile.name);
    if (missingStartLogs.length || missingSuccessLogs.length) {
      throw new Error("TubeDesigner profile preview logs are incomplete: " + JSON.stringify({ missingStartLogs, missingSuccessLogs, logMessages }));
    }
    tubeDesignerProfilePreviewResult = {
      profileCount: initialState.profiles.length,
      systemProfileCount: initialState.systemProfileCount,
      userProfileCount: initialState.userProfileCount,
      libraryGroups,
      expandedGroupLayout,
      listLayout,
      periodicBsplineTargetCount: results.filter((profile) => profile.sectionSvg.periodicBsplineTarget).length,
      periodicBsplineOuterCount: results.filter((profile) => profile.sectionSvg.periodicBsplineTarget
        && profile.sectionSvg.outerExists).length,
      progressCoverage,
      results,
      logMessages
    };
    const screenshotProfile = initialState.profiles.find((profile) =>
      profile.scope === "system" && profile.id === "round")
      ?? initialState.profiles.find((profile) => /04[_\s-].*圆管/i.test(profile.name))
      ?? initialState.profiles.find((profile) => /(?:^|[^椭])圆管/i.test(profile.name));
    if (screenshotProfile) {
      const selector = ".tube-profile-library-card[data-tube-designer-profile-key=\""
        + CSS.escape(screenshotProfile.selectionKey) + "\"]";
      const card = document.querySelector(selector);
      card?.click();
      await waitUntil(() => {
        const state = window.__icaxLaser3DCAM.getTubeDesignerProfileLibraryState();
        const status = document.querySelector("[data-tube-profile-preview-status] small")?.textContent?.trim() ?? "";
        return state.selectedProfileKey === screenshotProfile.selectionKey && !state.requestKey
          && state.appliedView?.entityIds?.length === 1
          && state.appliedView.entityIds[0] === screenshotProfile.selectionKey
          && status === "三维管型已生成" ? true : null;
      }, "round profile for final clipping screenshot");
      tubeDesignerProfilePreviewResult.screenshotProfileName = screenshotProfile.name;
      tubeDesignerProfilePreviewResult.screenshotProfileKey = screenshotProfile.selectionKey;
    }
  }

  let tubeDesignerResult = null;
  if ($checkTubeDesignerWorkflowLiteral) {
    if (typeof window.icax?.openDirectoryDialog !== "function") {
      throw new Error("TubeDesigner host directory picker is unavailable.");
    }
    const captureDesignerDom = () => {
      const sheetRows = Array.from(document.querySelectorAll(".tube-designer-sheet-row"));
      const productRows = Array.from(document.querySelectorAll("[data-tube-designer-product-row]"));
      const categoryRows = Array.from(document.querySelectorAll("[data-tube-designer-category-row]"));
      const partRows = Array.from(document.querySelectorAll("[data-tube-designer-part-row]"));
      const thumbnails = Array.from(document.querySelectorAll("[data-tube-designer-part-thumbnail]"));
      const partNames = Array.from(document.querySelectorAll(".tube-designer-tree-cell")).map((cell) => cell.textContent.trim());
      const partSpecifications = Array.from(document.querySelectorAll(".tube-designer-part-specification")).map((cell) => cell.textContent.trim());
      const resultTable = document.querySelector(".tube-designer-sheet");
      const selectionTable = document.querySelector(".tube-designer-selection-table");
      const breakdownSummary = document.querySelector("[data-tube-designer-breakdown-summary]")?.textContent ?? "";
      const partCountMatch = breakdownSummary.match(/·\s*(\d+)\s*个零件/);
      const selectedPartCountMatch = breakdownSummary.match(/已选择\s*(\d+)\s*个/);
      const parameterPanel = document.querySelector("[data-tube-designer-parameter-form]");
      const parameterHeader = parameterPanel?.querySelector(".tube-designer-parameter-header") ?? null;
      const parameterSections = Array.from(parameterPanel?.querySelectorAll("[data-tube-designer-parameter-group]") ?? []);
      const expandedParameterSections = parameterSections.filter((section) => section.open);
      const parameterSectionViewport = parameterPanel?.querySelector(".tube-designer-parameter-sections") ?? null;
      const visibleParameterFields = Array.from(parameterPanel?.querySelectorAll(".tube-designer-field") ?? [])
        .filter((field) => field.closest("[data-tube-designer-parameter-group]")?.open && field.getClientRects().length > 0);
      const error = document.querySelector(".cam-status.error")?.textContent?.trim() ?? "";
      const noticeElement = document.querySelector(".cam-status.notice");
      const noticeRect = noticeElement?.getBoundingClientRect();
      const inspectionViewport = document.querySelector("[data-tube-designer-part-inspection-viewport]");
      const inspectionProgress = document.querySelector("[data-tube-designer-inspection-progress]");
      const breakdownDialog = document.querySelector(".tube-designer-breakdown-dialog");
      const exportButton = document.querySelector("[data-tube-designer-export-selected]");
      const exportProgress = document.querySelector(".tube-designer-export-wait");
      return {
        error,
        ribbonCommandTitles: Array.from(document.querySelectorAll(".ribbon-command")).map((button) => button.textContent.trim()),
        ribbonCommandIds: Array.from(document.querySelectorAll(".ribbon-command")).map((button) => button.dataset.commandId),
        vectorRibbonIconCount: document.querySelectorAll(".ribbon-command .command-icon.vector svg").length,
        ribbonSplitToggleCount: document.querySelectorAll("[data-action='ribbon-command-menu-toggle']").length,
        visibleViewerHeaderCount: Array.from(document.querySelectorAll(".cam-viewer-head")).filter((node) => node.offsetParent !== null).length,
        addDialogCount: document.querySelectorAll(".tube-designer-config-dialog").length,
        templateCardCount: document.querySelectorAll(".tube-designer-template-card").length,
        instanceCardCount: document.querySelectorAll(".tube-designer-instance-card").length,
        parameterFormCount: document.querySelectorAll("[data-tube-designer-parameter-form]").length,
        disassemblyDialogCount: document.querySelectorAll(".tube-designer-selection-dialog").length,
        disassemblyInstanceRowCount: document.querySelectorAll("[data-tube-designer-disassembly-instance-row]").length,
        productGroupCount: productRows.length,
        categoryRowCount: categoryRows.length,
        visiblePartRowCount: partRows.length,
        sheetRowCount: sheetRows.length,
        productRowSpans: Array.from(document.querySelectorAll("[data-tube-designer-product-rowspan]"))
          .map((cell) => Number(cell.getAttribute("rowspan"))),
        resultTableBorderCollapse: resultTable ? getComputedStyle(resultTable).borderCollapse : "",
        resultTableColumnCount: resultTable?.querySelectorAll("thead th").length ?? 0,
        selectionTableBorderCollapse: selectionTable ? getComputedStyle(selectionTable).borderCollapse : "",
        nonNativeTableCellCount: Array.from(document.querySelectorAll(".tube-designer-sheet td, .tube-designer-selection-table td"))
          .filter((cell) => getComputedStyle(cell).display !== "table-cell").length,
        productGroupBottomOffsets: [],
        parameterSectionCount: parameterSections.length,
        expandedParameterSectionCount: expandedParameterSections.length,
        visibleParameterFieldCount: visibleParameterFields.length,
        maximumVisibleParameterFieldHeight: visibleParameterFields.length
          ? Math.max(...visibleParameterFields.map((field) => field.getBoundingClientRect().height))
          : 0,
        parameterSectionOverflowY: parameterSectionViewport
          ? getComputedStyle(parameterSectionViewport).overflowY
          : "",
        parameterActionButtonCount: parameterHeader?.querySelectorAll("[data-cam-action='tube-designer-confirm-update']").length ?? 0,
        parameterHeaderInsideScrollArea: parameterSectionViewport?.contains(parameterHeader) ?? false,
        parameterPanelOverflowY: parameterPanel ? getComputedStyle(parameterPanel).overflowY : "",
        parameterDisassemblyButtonCount: parameterPanel?.querySelectorAll("[data-cam-action='tube-designer-open-disassemble']").length ?? 0,
        partCount: Number(partCountMatch?.[1] ?? 0),
        selectedPartCount: Number(selectedPartCountMatch?.[1] ?? 0),
        thumbnailCount: thumbnails.length,
        resourceThumbnailCount: thumbnails.filter((canvas) => canvas.dataset.tubeThumbnailSource === "resource").length,
        partNames,
        partSpecifications,
        breakdownDialogCount: document.querySelectorAll(".tube-designer-breakdown-dialog").length,
        breakdownDialogBusy: breakdownDialog?.getAttribute("aria-busy") ?? "",
        exportButtonDisabled: exportButton?.disabled ?? false,
        exportProgressCount: document.querySelectorAll(".tube-designer-export-wait").length,
        exportProgressTitle: exportProgress?.querySelector("[data-tube-designer-export-progress-title]")?.textContent?.trim() ?? "",
        exportProgressMessage: exportProgress?.querySelector("[data-tube-designer-export-progress-message]")?.textContent?.trim() ?? "",
        operationProgressCount: document.querySelectorAll("[data-tube-designer-operation-wait]").length,
        operationProgressTitle: document.querySelector("[data-tube-designer-operation-title]")?.textContent?.trim() ?? "",
        operationProgressPhase: document.querySelector("[data-tube-designer-operation-phase]")?.textContent?.trim() ?? "",
        breakdownFooterCloseButtonCount: document.querySelectorAll(".tube-designer-breakdown-footer [data-cam-action='tube-designer-close-breakdown']").length,
        partInspectionDialogCount: document.querySelectorAll(".tube-designer-part-inspection-dialog").length,
        partInspectionCanvasCount: document.querySelectorAll("[data-tube-designer-part-inspection-viewport] canvas.icax-three-viewport-canvas").length,
        partInspectionReady: inspectionViewport?.dataset.tubeInspectionReady ?? "",
        partInspectionEntityCount: Number(inspectionViewport?.dataset.tubeInspectionEntityCount ?? 0),
        partInspectionDimensionCount: Number(inspectionViewport?.dataset.tubeInspectionDimensionCount ?? 0),
        partInspectionMeasurementMs: Number(inspectionViewport?.dataset.tubeInspectionMeasurementMs ?? 0),
        partInspectionProgressCount: document.querySelectorAll("[data-tube-designer-inspection-progress]").length,
        partInspectionProgressVisible: Boolean(inspectionProgress && !inspectionProgress.hidden),
        partInspectionProgressAriaHidden: inspectionProgress?.getAttribute("aria-hidden") ?? "",
        partInspectionProgressMs: Number(inspectionViewport?.dataset.tubeInspectionProgressMs ?? 0),
        partInspectionRenderMode: inspectionViewport?.dataset.tubeInspectionRenderMode ?? "",
        partInspectionPickingEnabled: inspectionViewport?.dataset.tubeInspectionPickingEnabled ?? "",
        partInspectionStatus: document.querySelector("[data-tube-designer-inspection-status]")?.textContent?.trim() ?? "",
        notice: noticeElement?.textContent?.trim() ?? "",
        noticeLayout: noticeElement ? {
          centerOffset: Math.abs((noticeRect.left + noticeRect.width / 2) - window.innerWidth / 2),
          top: noticeRect.top,
          pointerEvents: getComputedStyle(noticeElement).pointerEvents
        } : null
      };
    };

    const initialRibbonDom = captureDesignerDom();
    const batchAddOperation = await window.__icaxLaser3DCAM.executeAreaAction(
      "tube-designer-batch-add",
      { dataset: { tubeDesignerBatchPath: "D:\\orders\\security-windows.xlsx" } }
    );
    const batchAddState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const batchAddDom = captureDesignerDom();

    const initialDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const openCancelledAdd = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
    const addDialogDom = captureDesignerDom();
    const stateBeforeCancel = window.__icaxLaser3DCAM.getTubeDesignerState();
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-cancel-add");
    const cancelledAddDom = captureDesignerDom();
    const stateAfterCancel = window.__icaxLaser3DCAM.getTubeDesignerState();
    if (!openCancelledAdd?.handled || addDialogDom.addDialogCount !== 1 || addDialogDom.templateCardCount < 3 ||
        stateBeforeCancel.instances.length !== initialDesignerState.instances.length ||
        stateAfterCancel.instances.length !== initialDesignerState.instances.length || cancelledAddDom.addDialogCount !== 0) {
      throw new Error("TubeDesigner add/cancel contract failed: " + JSON.stringify({ addDialogDom, cancelledAddDom, initialDesignerState, stateBeforeCancel, stateAfterCancel }));
    }

    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
    const generatePromise = window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-add");
    const generateBusyDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.operationProgressCount === 1 && current.operationProgressTitle === "正在生成产品预览"
        ? current : null;
    }, "TubeDesigner centered generation progress");
    const generateOperation = await generatePromise;
    const generateCompletedDom = captureDesignerDom();
    const designerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const dom = captureDesignerDom();
    const viewport = window.__icaxLaser3DCAM.getViewportDebugState({
      samplePixels: true,
      includeObjects: true
    });
    if (!generateOperation?.handled || dom.error || dom.partCount !== 0 || designerState.parts.length !== 0) {
      throw new Error("TubeDesigner preview event chain failed: " + JSON.stringify({ generateOperation, dom, designerState }));
    }
    const widthInput = document.querySelector("[data-tube-designer-parameter='width']");
    if (!widthInput) {
      throw new Error("TubeDesigner regeneration controls are missing.");
    }
    widthInput.focus();
    widthInput.value = "1400";
    widthInput.dispatchEvent(new Event("change", { bubbles: true }));
    const parameterViewportBeforeRegenerate = document.querySelector(".tube-designer-parameter-sections");
    const parameterGroupsBeforeRegenerate = Array.from(
      document.querySelectorAll("[data-tube-designer-parameter-group]"));
    const lastParameterGroup = parameterGroupsBeforeRegenerate.at(-1);
    if (!parameterViewportBeforeRegenerate || !lastParameterGroup) {
      throw new Error("TubeDesigner parameter-panel state controls are missing.");
    }
    for (const parameterGroup of parameterGroupsBeforeRegenerate) parameterGroup.open = true;
    parameterViewportBeforeRegenerate.scrollTop = Math.min(
      180,
      Math.max(0, parameterViewportBeforeRegenerate.scrollHeight - parameterViewportBeforeRegenerate.clientHeight),
    );
    const parameterPanelStateBeforeRegenerate = {
      expandedGroups: parameterGroupsBeforeRegenerate
        .filter((group) => group.open)
        .map((group) => group.dataset.tubeDesignerParameterGroup),
      scrollTop: parameterViewportBeforeRegenerate.scrollTop,
    };
    const regenerateOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-update");
    const parameterViewportAfterRegenerate = document.querySelector(".tube-designer-parameter-sections");
    const parameterPanelStateAfterRegenerate = {
      expandedGroups: Array.from(document.querySelectorAll("[data-tube-designer-parameter-group][open]"))
        .map((group) => group.dataset.tubeDesignerParameterGroup),
      scrollTop: parameterViewportAfterRegenerate?.scrollTop ?? -1,
      focusedParameter: document.activeElement?.dataset?.tubeDesignerParameter ?? "",
    };
    const regeneratedDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const regeneratedDom = captureDesignerDom();
    const regeneratedViewport = window.__icaxLaser3DCAM.getViewportDebugState({
      samplePixels: true,
      includeObjects: true
    });
    if (!regenerateOperation?.handled || regeneratedDom.error) {
      throw new Error("TubeDesigner regeneration event chain failed: " + JSON.stringify({ regenerateOperation, regeneratedDom }));
    }
    await window.__icaxAppShell.executeRibbonCommand("edit.undo");
    const undoDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const undoDom = captureDesignerDom();
    const undoResult = {
      partCount: undoDom.partCount,
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true })
    };
    await window.__icaxAppShell.executeRibbonCommand("edit.redo");
    const redoDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const redoDom = captureDesignerDom();
    const redoResult = {
      partCount: redoDom.partCount,
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true })
    };
    const firstProductId = designerState.product.entityId;
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
    const compatibilityTemplateCardCount = document.querySelectorAll(
      ".tube-designer-template-card[data-tube-designer-template-id='security-window-1'], "
      + ".tube-designer-template-card[data-tube-designer-template-id='security-window-2'], "
      + ".tube-designer-template-card[data-tube-designer-template-id='security-window-3']"
    ).length;
    if (compatibilityTemplateCardCount !== 0) {
      throw new Error("TubeDesigner removed compatibility templates are still visible.");
    }
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-cancel-add");

    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
    const accessDoorFrameLayout = document.querySelector("[data-tube-designer-add-form] select[data-tube-designer-parameter='frameLayout']");
    if (!accessDoorFrameLayout) throw new Error("TubeDesigner frame-layout option is missing.");
    accessDoorFrameLayout.value = "four_sides";
    accessDoorFrameLayout.dispatchEvent(new Event("change", { bubbles: true }));
    const accessDoorConnectionProcess = await waitUntil(() => {
      const process = document.querySelector("[data-tube-designer-add-form] select[data-tube-designer-parameter='frameJoinType']");
      return process?.options.length === 6 ? process : null;
    }, "TubeDesigner complete connection-process catalog");
    accessDoorConnectionProcess.value = "v_groove_90:left_arc";
    accessDoorConnectionProcess.dispatchEvent(new Event("change", { bubbles: true }));
    await waitUntil(() =>
      document.querySelector("[data-tube-designer-add-form] select[data-tube-designer-parameter='frameJoinType']")?.value === "v_groove_90:left_arc",
    "TubeDesigner selected outer-frame connection process");
    const accessDoorToggle = document.querySelector("[data-tube-designer-add-form] input[type='checkbox'][data-tube-designer-parameter='accessDoorEnabled']");
    if (!accessDoorToggle) throw new Error("TubeDesigner access-door option is missing.");
    accessDoorToggle.checked = true;
    accessDoorToggle.dispatchEvent(new Event("change", { bubbles: true }));
    const accessDoorFrameProcess = await waitUntil(() => {
      const process = document.querySelector("[data-tube-designer-add-form] select[data-tube-designer-parameter='doorFrameJoinType']");
      return process?.options.length === 6 ? process : null;
    }, "TubeDesigner fixed-door-frame connection-process catalog");
    accessDoorFrameProcess.value = "miter_45";
    accessDoorFrameProcess.dispatchEvent(new Event("change", { bubbles: true }));
    const accessDoorLeafFrameProcess = await waitUntil(() => {
      const process = document.querySelector("[data-tube-designer-add-form] select[data-tube-designer-parameter='doorLeafFrameJoinType']");
      return process?.options.length === 6 ? process : null;
    }, "TubeDesigner door-leaf-frame connection-process catalog");
    accessDoorLeafFrameProcess.value = "v_groove_90:rounded_v";
    accessDoorLeafFrameProcess.dispatchEvent(new Event("change", { bubbles: true }));
    const accessDoorParameterDom = await waitUntil(() => {
      const form = document.querySelector("[data-tube-designer-add-form]");
      const doorLeft = form?.querySelector("[data-tube-designer-parameter='doorLeft']");
      const doorFrameRadius = form?.querySelector("[data-tube-designer-parameter='doorFrameCornerRadius']");
      const doorVerticalType = form?.querySelector("[data-tube-designer-parameter='doorVerticalProfileType']");
      const profileTypeFields = Array.from(form?.querySelectorAll("select[data-tube-designer-parameter$='ProfileType']") ?? []);
      const profileTypeLabels = profileTypeFields.map((field) => field.selectedOptions[0]?.textContent?.trim() ?? "");
      const outerProcess = form?.querySelector("select[data-tube-designer-parameter='frameJoinType']");
      const fixedProcess = form?.querySelector("select[data-tube-designer-parameter='doorFrameJoinType']");
      const leafProcess = form?.querySelector("select[data-tube-designer-parameter='doorLeafFrameJoinType']");
      return doorLeft && doorFrameRadius && doorVerticalType?.value === "round"
        && profileTypeFields.length === 7
        && profileTypeLabels.join("|") === "矩形管|矩形管|圆管|矩形管|矩形管|矩形管|圆管"
        && outerProcess?.value === "v_groove_90:left_arc"
        && fixedProcess?.value === "miter_45"
        && leafProcess?.value === "v_groove_90:rounded_v"
        ? {
            doorLeft: doorLeft.value,
            doorFrameRadius: doorFrameRadius.value,
            doorVerticalType: doorVerticalType.value,
            profileTypeLabels,
            outerProcess: outerProcess.value,
            outerProcessCount: outerProcess.options.length,
            fixedProcess: fixedProcess.value,
            fixedProcessCount: fixedProcess.options.length,
            leafProcess: leafProcess.value,
            leafProcessCount: leafProcess.options.length,
          }
        : null;
    }, "TubeDesigner access-door parameter form");
    const accessDoorPreviewOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-add");
    const accessDoorDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const accessDoorViewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });
    const accessDoorInspectorDom = captureDesignerDom();
    const accessDoorProductId = accessDoorDesignerState.product.entityId;
    const secondProductId = accessDoorProductId;
    const expectedDisassemblyProductCount = 2;
    const expectedDisassembledPartCount = regeneratedDesignerState.members.length
      + accessDoorDesignerState.members.length;

    const activateFirstOperation = await window.__icaxLaser3DCAM.executeAreaAction(
      "tube-designer-select-instance",
      { dataset: { tubeDesignerInstanceId: firstProductId } }
    );
    const firstActivatedState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const firstActivatedViewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });
    const firstActivationNoticeDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.notice.startsWith("已切换到 ") ? current : null;
    }, "TubeDesigner activation notice");
    const dismissedActivationNoticeDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.notice === "" ? current : null;
    }, "TubeDesigner activation notice dismissal");
    const activateSecondOperation = await window.__icaxLaser3DCAM.executeAreaAction(
      "tube-designer-select-instance",
      { dataset: { tubeDesignerInstanceId: secondProductId } }
    );
    const secondActivatedState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const secondActivatedViewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });

    const disassembleOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-disassemble");
    const disassemblySelectionDom = captureDesignerDom();
    const disassembleConfirmOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-disassemble");
    const disassembledDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const breakdownDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.productGroupCount === expectedDisassemblyProductCount
        && current.partCount === expectedDisassembledPartCount
        && current.selectedPartCount === expectedDisassembledPartCount && current.categoryRowCount > 0 &&
        current.resourceThumbnailCount === current.categoryRowCount ? current : null;
    }, "TubeDesigner grouped manufacturing list and resource thumbnails");
    const firstCategoryToggle = document.querySelector(
      "[data-tube-designer-category-row] [data-cam-action='tube-designer-toggle-category-tree']");
    if (!firstCategoryToggle) throw new Error("TubeDesigner category expansion control is missing.");
    firstCategoryToggle.click();
    await waitUntil(
      () => document.querySelector("[data-tube-designer-part-row]") ?? null,
      "TubeDesigner category child rows"
    );
    const partInspectionLink = document.querySelector(
      "[data-tube-designer-part-row] [data-cam-action='tube-designer-open-part-inspection']");
    if (!partInspectionLink) throw new Error("TubeDesigner part inspection control is missing.");
    partInspectionLink.click();
    const partInspectionProgressDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.partInspectionDialogCount === 1
        && current.partInspectionProgressCount === 1
        && current.partInspectionProgressVisible
        && current.partInspectionProgressAriaHidden === "false" ? current : null;
    }, "TubeDesigner part inspection progress indicator");
    const partInspectionDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.partInspectionDialogCount === 1
        && current.partInspectionCanvasCount === 1
        && current.partInspectionReady === "true"
        && current.partInspectionEntityCount === 1
        && current.partInspectionDimensionCount > 0 ? current : null;
    }, "TubeDesigner isolated part inspection viewport");
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-close-part-inspection");
    const closedPartInspectionDom = captureDesignerDom();
    const partialSelectionCheckbox = Array.from(
      document.querySelectorAll("[data-tube-designer-part-row] input[data-cam-action='tube-designer-toggle-part']"))[0];
    if (!partialSelectionCheckbox) throw new Error("TubeDesigner manufacturing selection controls are missing.");
    const breakdownDialogBeforeSelection = document.querySelector(".tube-designer-breakdown-dialog");
    const breakdownTableBeforeSelection = document.querySelector(".tube-designer-sheet");
    const firstThumbnailBeforeSelection = document.querySelector("[data-tube-designer-part-thumbnail]");
    partialSelectionCheckbox.click();
    const partialSelectionDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.selectedPartCount === expectedDisassembledPartCount - 1 ? current : null;
    }, "TubeDesigner partial manufacturing selection");
    const partialSelectionPreservedDom = breakdownDialogBeforeSelection === document.querySelector(".tube-designer-breakdown-dialog")
      && breakdownTableBeforeSelection === document.querySelector(".tube-designer-sheet")
      && firstThumbnailBeforeSelection === document.querySelector("[data-tube-designer-part-thumbnail]");
    let exportResult = null;
    let exportBusyDom = null;
    let exportLockedDom = null;
    let exportCompletedDom = null;
    let duplicateExportResult = null;
    if ($checkTubeDesignerStepExportLiteral) {
      const exportPromise = window.__icaxLaser3DCAM.executeAreaAction(
        "tube-designer-export-selected",
        { dataset: { tubeDesignerExportDirectory: $tubeDesignerExportDirectoryLiteral } }
      );
      exportBusyDom = await waitUntil(() => {
        const current = captureDesignerDom();
        return current.breakdownDialogBusy === "true" && current.exportProgressCount === 1
          && current.exportButtonDisabled ? current : null;
      }, "TubeDesigner export waiting state");
      duplicateExportResult = await window.__icaxLaser3DCAM.executeAreaAction(
        "tube-designer-export-selected",
        { dataset: { tubeDesignerExportDirectory: $tubeDesignerExportDirectoryLiteral } }
      );
      await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-close-breakdown");
      exportLockedDom = captureDesignerDom();
      exportResult = await exportPromise;
      if (!exportResult?.handled) throw new Error("TubeDesigner STEP export event chain failed: " + JSON.stringify(exportResult));
      exportCompletedDom = captureDesignerDom();
    }
    await window.__icaxAppShell.executeRibbonCommand("edit.undo");
    const undoDisassemblyDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const undoDisassemblyResult = {
      dom: captureDesignerDom(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true })
    };
    await window.__icaxAppShell.executeRibbonCommand("edit.redo");
    const redoDisassemblyDesignerState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const redoDisassemblyResult = {
      dom: captureDesignerDom(),
      viewport: window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true })
    };
    const reopenBreakdownOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-breakdown");
    const finalBreakdownDom = await waitUntil(() => {
      const current = captureDesignerDom();
      return current.breakdownDialogCount === 1
        && current.productGroupCount === expectedDisassemblyProductCount ? current : null;
    }, "TubeDesigner reopened grouped manufacturing list");
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-close-breakdown");
    const finalAccessDoorActivation = await window.__icaxLaser3DCAM.executeAreaAction(
      "tube-designer-select-instance",
      { dataset: { tubeDesignerInstanceId: accessDoorProductId } }
    );
    const finalAccessDoorState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const finalAccessDoorViewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });
    const generateMultiFaceTemplate = async (templateId, expectedMemberCount) => {
      await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
      const card = document.querySelector(
        ".tube-designer-template-card[data-tube-designer-template-id='" + templateId + "']");
      if (!card || card.disabled) throw new Error("TubeDesigner template " + templateId + " is unavailable.");
      card.click();
      await waitUntil(
        () => document.querySelector(".tube-designer-template-card.selected[data-tube-designer-template-id='" + templateId + "']")
          && document.querySelector("[data-tube-designer-add-form] [data-tube-designer-parameter='frontWidth']"),
        "TubeDesigner " + templateId + " parameter form",
      );
      const operation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-add");
      const state = window.__icaxLaser3DCAM.getTubeDesignerState();
      const viewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });
      if (!operation?.handled || state.product?.templateId !== templateId
          || state.members?.length !== expectedMemberCount
          || viewport.visibleObjectCount !== expectedMemberCount) {
        throw new Error("TubeDesigner " + templateId + " preview failed: " + JSON.stringify({ operation, state, viewport }));
      }
      return { operation, state, viewport };
    };
    const twoFaceTemplateResult = await generateMultiFaceTemplate("two-face-security-window", 28);
    const threeFaceTemplateResult = await generateMultiFaceTemplate("three-face-security-window", 39);
    const fiveFaceTemplateResult = await generateMultiFaceTemplate("five-face-security-window", 63);
    await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-open-add");
    const stairTemplateCard = document.querySelector(
      ".tube-designer-template-card[data-tube-designer-template-id='straight-stair-railing']");
    if (!stairTemplateCard || stairTemplateCard.disabled) {
      throw new Error("TubeDesigner stair industry template is unavailable.");
    }
    stairTemplateCard.click();
    await waitUntil(
      () => document.querySelector(".tube-designer-template-card.selected[data-tube-designer-template-id='straight-stair-railing']")
        && document.querySelector("[data-tube-designer-add-form] [data-tube-designer-parameter='flightRun']")
        && document.querySelector("[data-tube-designer-add-form] [data-tube-designer-parameter='infillType']"),
      "TubeDesigner stair industry parameter form",
    );
    const stairIndustryGroupCount = document.querySelectorAll(".tube-designer-template-industry").length;
    const stairTemplateOperation = await window.__icaxLaser3DCAM.executeAreaAction("tube-designer-confirm-add");
    const stairTemplateState = window.__icaxLaser3DCAM.getTubeDesignerState();
    const stairTemplateViewport = window.__icaxLaser3DCAM.getViewportDebugState({ samplePixels: false, includeObjects: true });
    const stairTemplateResult = {
      operation: stairTemplateOperation,
      state: stairTemplateState,
      viewport: stairTemplateViewport,
      industryGroupCount: stairIndustryGroupCount,
    };
    tubeDesignerResult = {
      initialDesignerState,
      initialRibbonDom,
      batchAddOperation,
      batchAddState,
      batchAddDom,
      addDialogDom,
      cancelledAddDom,
      generateOperation,
      generateBusyDom,
      generateCompletedDom,
      designerState,
      dom,
      viewport,
      regenerateOperation,
      parameterPanelStateBeforeRegenerate,
      parameterPanelStateAfterRegenerate,
      regeneratedDesignerState,
      regeneratedDom,
      regeneratedViewport,
      undoResult,
      undoDesignerState,
      redoResult,
      redoDesignerState,
      firstProductId,
      disassembleOperation,
      disassemblySelectionDom,
      disassembleConfirmOperation,
      disassembledDesignerState,
      breakdownDom,
      partInspectionProgressDom,
      partInspectionDom,
      closedPartInspectionDom,
      partialSelectionDom,
      partialSelectionPreservedDom,
      exportResult,
      exportBusyDom,
      exportLockedDom,
      exportCompletedDom,
      duplicateExportResult,
      undoDisassemblyDesignerState,
      undoDisassemblyResult,
      redoDisassemblyDesignerState,
      redoDisassemblyResult,
      reopenBreakdownOperation,
      finalBreakdownDom,
      compatibilityTemplateCardCount,
      secondProductId,
      expectedDisassemblyProductCount,
      expectedDisassembledPartCount,
      accessDoorParameterDom,
      accessDoorPreviewOperation,
      accessDoorDesignerState,
      accessDoorViewport,
      accessDoorInspectorDom,
      accessDoorProductId,
      finalAccessDoorActivation,
      finalAccessDoorState,
      finalAccessDoorViewport,
      twoFaceTemplateResult,
      threeFaceTemplateResult,
      fiveFaceTemplateResult,
      stairTemplateResult,
      activateFirstOperation,
      firstActivatedState,
      firstActivatedViewport,
      firstActivationNoticeDom,
      dismissedActivationNoticeDom,
      activateSecondOperation,
      secondActivatedState,
      secondActivatedViewport
    };
  }

  const tubeMainDomState = $checkTubeCSGWorkflowLiteral
    ? await waitUntil(() => {
        const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
        return state
          && state.mainWorkspaceCount === 1
          && state.workpieceCardCount > 0
          && state.resourceWorkpieceThumbnailCount === state.workpieceCardCount
          && state.layerStripCount === 1
          && state.bottomDockCount === 1
          && state.mainPropertyPaneCount === 1
          && state.workpiecePaginationCount === 1
          && state.nestingResultListCount === 1
          && state.bottomTabButtonCount === 2
          && state.activeBottomTab === "nesting"
          && state.nestingBottomPanelCount === 1
          && state.stockBottomPanelCount === 0
          && state.bottomSplitterCount === 1
          && state.visibleMainViewCubeCount === 1
          && state.visibleMainAxisCount === 1
          ? state
          : null;
      }, "TubesT-style TubeOne main workspace")
    : null;

  let tubeBottomTabResult = null;
  let tubeBottomResizeResult = null;
  let tubeToolbarTooltipResult = null;
  let tubeWorkpieceSearchResult = null;
  let tubeFilteredSelectionResult = null;
  if ($checkTubeCSGWorkflowLiteral) {
    const searchInput = document.querySelector("[data-tube-workpiece-search]");
    const firstWorkpieceCard = document.querySelector(".tube-workpiece-card");
    if (!searchInput || !firstWorkpieceCard) {
      throw new Error("TubeOne workpiece query input is missing.");
    }
    const runWorkpieceQuery = async (query, expectedVisible) => {
      searchInput.value = query;
      searchInput.dispatchEvent(new Event("input", { bubbles: true }));
      return await waitUntil(() => {
        const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
        return state?.workpieceSearchValue === query
          && state.visibleWorkpieceCardCount === expectedVisible
          ? state
          : null;
      }, "TubeOne workpiece query: " + query);
    };
    const originalProcessTerms = firstWorkpieceCard.dataset.tubeWorkpieceProcesses ?? "";
    const byName = await runWorkpieceQuery("n:double n:cavity", 1);
    firstWorkpieceCard.dataset.tubeWorkpieceProcesses = (originalProcessTerms + " 坡口 贯切").trim();
    const byProcess = await runWorkpieceQuery("t:坡口", 1);
    const excludedProcess = await runWorkpieceQuery("-t:坡口", 0);
    const booleanOr = await runWorkpieceQuery("n:不存在 or t:贯切", 1);
    const invalidFilter = await runWorkpieceQuery("x:未知", 0);
    firstWorkpieceCard.dataset.tubeWorkpieceProcesses = originalProcessTerms;
    const cleared = await runWorkpieceQuery("", 1);
    tubeWorkpieceSearchResult = { byName, byProcess, excludedProcess, booleanOr, invalidFilter, cleared };
    document.querySelector("[data-cam-action='tube-select-filtered-workpieces']")?.click();
    const selectedAll = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.selectedWorkpieceCardCount === 1 ? state : null;
    }, "selected filtered TubeOne workpieces");
    document.querySelector("[data-cam-action='tube-invert-filtered-workpieces']")?.click();
    const inverted = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.selectedWorkpieceCardCount === 0 ? state : null;
    }, "inverted filtered TubeOne workpieces");
    tubeFilteredSelectionResult = { selectedAll, inverted };
    const toolbarTooltipTarget = document.querySelector(".tubest-part-toolbar button:not([disabled])");
    if (!toolbarTooltipTarget) {
      throw new Error("TubeOne toolbar tooltip target is missing.");
    }
    toolbarTooltipTarget.dispatchEvent(new PointerEvent("pointerover", { bubbles: true }));
    const shownTooltip = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.visibleToolbarTooltipCount === 1 && state.toolbarTooltipText ? state : null;
    }, "TubeOne toolbar bubble tooltip");
    toolbarTooltipTarget.dispatchEvent(new PointerEvent("pointerout", { bubbles: true }));
    const hiddenTooltip = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.visibleToolbarTooltipCount === 0 ? state : null;
    }, "hidden TubeOne toolbar bubble tooltip");
    tubeToolbarTooltipResult = { shownTooltip, hiddenTooltip };
    const bottomHandle = document.querySelector("[data-cam-resize-pane='bottom']");
    const leftHandle = document.querySelector("[data-cam-resize-pane='left']");
    const rightHandle = document.querySelector("[data-cam-resize-pane='right']");
    const mainWorkbench = document.querySelector(".cam-workbench");
    if (!leftHandle || !rightHandle || !bottomHandle || !mainWorkbench) {
      throw new Error("TubeOne main workspace resize handle is missing.");
    }
    const parsePx = (value) => Number(String(value ?? "").trim().replace("px", ""));
    const dragHandle = async (handle, deltaX, deltaY, pointerId) => {
      const rect = handle.getBoundingClientRect();
      const startX = rect.left + rect.width / 2;
      const startY = rect.top + rect.height / 2;
      handle.dispatchEvent(new PointerEvent("pointerdown", {
        bubbles: true, button: 0, pointerId, clientX: startX, clientY: startY
      }));
      window.dispatchEvent(new PointerEvent("pointermove", {
        bubbles: true, pointerId, clientX: startX + deltaX, clientY: startY + deltaY
      }));
      window.dispatchEvent(new PointerEvent("pointerup", {
        bubbles: true, pointerId, clientX: startX + deltaX, clientY: startY + deltaY
      }));
      await delay(100);
    };
    const beforeLeftWidth = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-left-width"));
    await dragHandle(leftHandle, 36, 0, 50);
    const afterLeftWidth = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-left-width"));
    const beforeRightWidth = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-right-width"));
    await dragHandle(rightHandle, -36, 0, 51);
    const afterRightWidth = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-right-width"));
    const beforeBottomHeight = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-bottom-height"));
    await dragHandle(bottomHandle, 0, -36, 52);
    const afterBottomHeight = parsePx(getComputedStyle(mainWorkbench).getPropertyValue("--cam-bottom-height"));
    tubeBottomResizeResult = {
      beforeLeftWidth, afterLeftWidth,
      beforeRightWidth, afterRightWidth,
      beforeBottomHeight, afterBottomHeight
    };
    document.querySelector("[data-cam-action='tube-bottom-tab'][data-tube-bottom-tab='stock']")?.click();
    const stock = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.activeBottomTab === "stock"
        && state.stockBottomPanelCount === 1
        && state.nestingBottomPanelCount === 0
        ? state
        : null;
    }, "TubeOne stock bottom tab");
    document.querySelector("[data-cam-action='tube-bottom-tab'][data-tube-bottom-tab='nesting']")?.click();
    const nesting = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
      return state?.activeBottomTab === "nesting"
        && state.nestingBottomPanelCount === 1
        && state.stockBottomPanelCount === 0
        ? state
        : null;
    }, "TubeOne nesting bottom tab");
    tubeBottomTabResult = { stock, nesting };
  }

  let recognizeCADIntentResult = null;
  if ($recognizeCADIntentLiteral) {
    recognizeCADIntentResult = await window.__icaxLaser3DCAM.recognizeCADIntent();
    await delay(300);
  }
  const tubePreviewResults = [];
  let tubeParameterEditResult = null;
  if ($checkTubeCSGWorkflowLiteral) {
    const tubeGeometry = window.__icaxLaser3DCAM?.getState?.()?.tubeGeometry ?? {};
    const intentNodes = (tubeGeometry.solidNodes ?? []).concat(tubeGeometry.sectionPrimitives ?? []);
    const editableTool = (tubeGeometry.solidNodes ?? []).find((node) =>
      node.materialRole
        && node.materialRole !== "None"
        && node.previewAvailable
        && (node.parameters ?? []).some((item) => item.editable !== false));
    const parameterNodeId = editableTool?.id
      ?? window.__icaxLaser3DCAM?.getState?.()?.selectedCADIntentNodeId
      ?? "";
    const previewNodes = intentNodes
      .filter((node) => node.previewAvailable);
    for (const node of previewNodes) {
      const selection = await window.__icaxLaser3DCAM.selectCADIntentNode(node.id);
      tubePreviewResults.push({
        nodeId: node.id,
        selectedNodeId: selection.tubeDom.selectedNodeId,
        previewNodeId: selection.tubeDom.previewNodeId,
        ghostPreviewVisible: selection.viewport?.ghostPreviewVisible ?? false,
        ghostPreviewVertexCount: selection.viewport?.ghostPreviewVertexCount ?? 0
      });
    }
    if (parameterNodeId) {
      await window.__icaxLaser3DCAM.selectCADIntentNode(parameterNodeId);
      const initialGeometry = window.__icaxLaser3DCAM.getState().tubeGeometry ?? {};
      const initialNode = (initialGeometry.solidNodes ?? [])
        .concat(initialGeometry.sectionPrimitives ?? [])
        .find((node) => node.id === parameterNodeId);
      const parameter = (initialNode?.parameters ?? [])
        .find((item) => item.editable !== false && item.name === "length")
        ?? (initialNode?.parameters ?? []).find((item) => item.editable !== false);
      if (parameter) {
        const originalValue = Number(parameter.value);
        const changedValue = originalValue + (String(parameter.name).toLowerCase().includes("scale") ? 0.1 : 1);
        const livePreview = await window.__icaxLaser3DCAM.previewCADIntentParameter(
          parameterNodeId,
          parameter.name,
          changedValue
        );
        const changed = await window.__icaxLaser3DCAM.setCADIntentParameter(
          parameterNodeId,
          parameter.name,
          changedValue
        );
        const reverted = await window.__icaxLaser3DCAM.setCADIntentParameter(
          parameterNodeId,
          parameter.name,
          originalValue
        );
        tubeParameterEditResult = { originalValue, changedValue, livePreview, changed, reverted };
      }
    }
  }
  const tubeCADIntentDomState = $checkTubeCSGWorkflowLiteral
    ? await waitUntil(() => {
        const state = window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null;
        return state
          && state.editorWorkspaceCount === 1
          && state.manufacturingTreeCount === 1
          && state.historyNodeCount > 0
          && state.layerStripCount === 0
          ? state
          : null;
      }, "TubeOne three-dimensional part editor")
    : (window.__icaxLaser3DCAM?.getTubeCADIntentDomState?.() ?? null);

  let machineEnableResult = null;
  if ($checkMachineEnableWorkflowLiteral) {
    const before = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getState?.();
      return state?.machineInstances?.length ? state : null;
    }, "machine instance for enable workflow");
    const machineId = before.machineInstances[0].id;
    await window.__icaxLaser3DCAM.setMachineInstanceEnabled(machineId, false);
    await delay(200);
    const afterDisable = window.__icaxLaser3DCAM.getState();
    await window.__icaxLaser3DCAM.setMachineInstanceEnabled(machineId, true);
    await delay(200);
    const afterEnable = window.__icaxLaser3DCAM.getState();
    machineEnableResult = { machineId, before, afterDisable, afterEnable };
  }

  let machineRenameResult = null;
  if ($checkMachineRenameWorkflowLiteral) {
    const before = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getState?.();
      return state?.machineInstances?.length ? state : null;
    }, "machine instance for rename workflow");
    const machineId = before.machineInstances[0].id;
    const targetName = "UI Smoke Renamed Machine";
    await window.__icaxLaser3DCAM.setMachineInstanceName(machineId, targetName);
    const afterRename = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getState?.();
      return state?.machineInstances?.some((item) => item?.id === machineId && item?.name === targetName) ? state : null;
    }, "renamed machine instance");
    machineRenameResult = { machineId, targetName, before, afterRename };
  }

  let machineSelectionResult = null;
  if ($checkMachineSelectionWorkflowLiteral) {
    const viewportWithObjects = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getViewportDebugState?.({ samplePixels: true, includeObjects: true });
      const object = state?.objects?.find((item) =>
        item?.visible && Number(item?.renderClass) === 4 && typeof item?.objectId === "string" && item.objectId.length > 0);
      return object ? { state, object } : null;
    }, "machine render object for selection");
    machineSelectionResult = await window.__icaxLaser3DCAM.pickMachineObject({
      objectId: viewportWithObjects.object.objectId,
      kind: "machine.visual",
      label: "smoke machine object"
    });
    machineSelectionResult.expectedObjectId = viewportWithObjects.object.objectId;
    machineSelectionResult.domAfterPick = await waitUntil(() => {
      const dom = window.__icaxLaser3DCAM?.getMachineDomState?.();
      return dom?.hasAppearanceEditor && dom?.hasCollisionToggle && dom?.selectedTreeRows > 0 ? dom : null;
    }, "machine property editor after selection");
    machineSelectionResult.appearance = await window.__icaxLaser3DCAM.setMachineElementAppearance({
      entityId: viewportWithObjects.object.objectId,
      colorHex: "#cc6633",
      showCollision: true
    });
    machineSelectionResult.collider = await waitUntil(() => {
      const state = window.__icaxLaser3DCAM?.getViewportDebugState?.({ samplePixels: true, includeObjects: true });
      return Number(state?.visibleColliderObjectCount ?? 0) > 0 && Number(state?.colliderShapeCount ?? 0) > 0
        ? state
        : null;
    }, "machine ColliderPDO wire objects");
    machineSelectionResult.standardView = await window.__icaxLaser3DCAM.setStandardView("top-front-right");
  }

  let workbenchResizeResult = null;
  if ($checkWorkbenchResizeWorkflowLiteral) {
    const parsePx = (value) => Number(String(value ?? "").trim().replace("px", ""));
    const dragHandle = async (selector, deltaX) => {
      const handle = document.querySelector(selector);
      const workbench = document.querySelector(".cam-workbench");
      if (!handle || !workbench) {
        throw new Error("Workbench resize handle is missing: " + selector);
      }
      const rect = handle.getBoundingClientRect();
      const startX = rect.left + rect.width / 2;
      handle.dispatchEvent(new PointerEvent("pointerdown", {
        bubbles: true,
        button: 0,
        pointerId: 41,
        clientX: startX,
        clientY: rect.top + rect.height / 2
      }));
      window.dispatchEvent(new PointerEvent("pointermove", {
        bubbles: true,
        pointerId: 41,
        clientX: startX + deltaX,
        clientY: rect.top + rect.height / 2
      }));
      window.dispatchEvent(new PointerEvent("pointerup", {
        bubbles: true,
        pointerId: 41,
        clientX: startX + deltaX,
        clientY: rect.top + rect.height / 2
      }));
      await delay(100);
      return workbench;
    };

    const workbench = await waitUntil(() => document.querySelector(".cam-workbench"), "CAM workbench");
    const beforeLeft = parsePx(getComputedStyle(workbench).getPropertyValue("--cam-left-width"));
    const beforeRight = parsePx(getComputedStyle(workbench).getPropertyValue("--cam-right-width"));
    await dragHandle("[data-cam-resize-pane='left']", 70);
    const afterLeft = parsePx(getComputedStyle(workbench).getPropertyValue("--cam-left-width"));
    await dragHandle("[data-cam-resize-pane='right']", -60);
    const afterRight = parsePx(getComputedStyle(workbench).getPropertyValue("--cam-right-width"));
    workbenchResizeResult = { beforeLeft, afterLeft, beforeRight, afterRight };
  }

  const viewport = defaultMachineViewport ?? window.__icaxLaser3DCAM?.getViewportDebugState?.({ samplePixels: true, includeObjects: $checkMachineSelectionWorkflowLiteral }) ?? null;
  return {
    href: location.href,
    title: document.title,
    app: window.__icaxAppShell?.getState?.() ?? null,
    productDiagnosticsReady: Boolean(window.__icaxLaser3DCAM),
    product: window.__icaxLaser3DCAM?.getState?.() ?? null,
    createProjectResult,
    defaultMachineResult,
    importMachineResult,
    importWorkpieceResult,
    tubeDesignerProfilePreviewResult,
    tubeDesignerResult,
    recognizeCADIntentResult,
    tubeMainDomState,
    tubeBottomTabResult,
    tubeBottomResizeResult,
    tubeToolbarTooltipResult,
    tubeWorkpieceSearchResult,
    tubeFilteredSelectionResult,
    tubeCADIntentDomState,
    tubePreviewResults,
    tubeParameterEditResult,
    machineEnableResult,
    machineRenameResult,
    machineSelectionResult,
    machineDomState: window.__icaxLaser3DCAM?.getMachineDomState?.() ?? null,
    workbenchResizeResult,
    viewport,
    bodyText: (document.body?.innerText ?? "").slice(0, 1200)
  };
})()
"@

        $response = Invoke-CdpCommand -Socket $socket -Id 1 -Method "Runtime.evaluate" -Params @{
            expression = $expression
            returnByValue = $true
            awaitPromise = $true
        }
        if ($response.result.exceptionDetails) {
            $exceptionText = $response.result.exceptionDetails | ConvertTo-Json -Depth 16 -Compress
            throw "Frontend automation expression failed: $exceptionText"
        }
        $state = $response.result.result.value
        $state | ConvertTo-Json -Depth 32

        if ($CheckTubeDesignerProfilePreview) {
            if (-not $state.tubeDesignerProfilePreviewResult -or
                [int]$state.tubeDesignerProfilePreviewResult.systemProfileCount -ne 11 -or
                [int]$state.tubeDesignerProfilePreviewResult.profileCount -ne
                    ([int]$state.tubeDesignerProfilePreviewResult.systemProfileCount +
                     [int]$state.tubeDesignerProfilePreviewResult.userProfileCount) -or
                [int]$state.tubeDesignerProfilePreviewResult.libraryGroups.groupCount -ne 2 -or
                $state.tubeDesignerProfilePreviewResult.libraryGroups.systemExists -ne $true -or
                $state.tubeDesignerProfilePreviewResult.libraryGroups.userExists -ne $true -or
                [int]$state.tubeDesignerProfilePreviewResult.libraryGroups.systemCardCount -ne 11 -or
                [int]$state.tubeDesignerProfilePreviewResult.libraryGroups.systemCardCount -ne
                    [int]$state.tubeDesignerProfilePreviewResult.systemProfileCount -or
                [int]$state.tubeDesignerProfilePreviewResult.libraryGroups.userCardCount -ne
                    [int]$state.tubeDesignerProfilePreviewResult.userProfileCount -or
                [int]$state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.cardRects.Count -ne 11 -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.allCardsHaveHeight -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.groupContainsAllCards -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.contentContainsAllCards -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.groupHasNoClippedOverflow -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.system.contentHasNoClippedOverflow -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.user.allCardsHaveHeight -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.user.groupContainsAllCards -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.user.contentContainsAllCards -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.user.groupHasNoClippedOverflow -ne $true -or
                $state.tubeDesignerProfilePreviewResult.expandedGroupLayout.user.contentHasNoClippedOverflow -ne $true -or
                $state.tubeDesignerProfilePreviewResult.listLayout.hasVerticalOverflow -ne $true -or
                $state.tubeDesignerProfilePreviewResult.listLayout.lastSystemCardFullyVisible -ne $true -or
                [int]$state.tubeDesignerProfilePreviewResult.periodicBsplineTargetCount -lt 1 -or
                [int]$state.tubeDesignerProfilePreviewResult.periodicBsplineOuterCount -ne
                    [int]$state.tubeDesignerProfilePreviewResult.periodicBsplineTargetCount -or
                $state.tubeDesignerProfilePreviewResult.progressCoverage.coversViewport -ne $true -or
                @($state.tubeDesignerProfilePreviewResult.results).Count -ne
                    [int]$state.tubeDesignerProfilePreviewResult.profileCount) {
                $profileText = $state.tubeDesignerProfilePreviewResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner profile 3D preview workflow failed: $profileText"
            }
            foreach ($profile in @($state.tubeDesignerProfilePreviewResult.results)) {
                $expectedSelectionKey = "$($profile.scope):$($profile.id)"
                $expectedRevisionPrefix = "profile-preview:${expectedSelectionKey}:"
                if ([string]::IsNullOrWhiteSpace([string]$profile.id) -or
                    ($profile.scope -ne "system" -and $profile.scope -ne "user") -or
                    $profile.selectionKey -ne $expectedSelectionKey -or
                    $profile.appliedEntityId -ne $expectedSelectionKey -or
                    $profile.viewportObjectId -ne $expectedSelectionKey -or
                    -not ([string]$profile.appliedRevision).StartsWith($expectedRevisionPrefix) -or
                    $profile.status -ne "三维管型已生成" -or
                    $profile.progressObserved -ne $true -or
                    $profile.previewWaitObserved -ne $true -or
                    $profile.previewWaitCoversViewport -ne $true -or
                    [int]$profile.elapsedMs -lt 500 -or
                    [int]$profile.elapsedMs -ge 10000 -or
                    [int]$profile.visibleObjectCount -ne 1 -or
                    [int]$profile.nonBackgroundPixels -le 0 -or
                    [uint64]$profile.geometryResourceVersion -le 0) {
                    $profileText = $profile | ConvertTo-Json -Depth 10 -Compress
                    throw "TubeDesigner profile 3D preview is not renderable: $profileText"
                }
                if (-not $profile.sectionSvg -or
                    $profile.sectionSvg.svgExists -ne $true -or
                    $profile.sectionSvg.outerExists -ne $true -or
                    $profile.sectionSvg.geometryExists -ne $true -or
                    $profile.sectionSvg.svgElementContained -ne $true -or
                    $profile.sectionSvg.svgElementSizedToFrame -ne $true -or
                    $profile.sectionSvg.fullyContained -ne $true -or
                    $profile.sectionSvg.hasSafePadding -ne $true -or
                    [double]$profile.sectionSvg.viewBox.width -le 0 -or
                    [double]$profile.sectionSvg.viewBox.height -le 0) {
                    $profileText = $profile | ConvertTo-Json -Depth 16 -Compress
                    throw "TubeDesigner profile section SVG is missing or clipped: $profileText"
                }
                if ($profile.sectionSvg.curveExpected -eq $true -and
                    $profile.sectionSvg.smoothCurveExpression -ne $true) {
                    $profileText = $profile | ConvertTo-Json -Depth 16 -Compress
                    throw "TubeDesigner curved profile section regressed to a coarse straight polygon: $profileText"
                }
            }
        }

        if ($CheckTubeDesignerWorkflow) {
            if (-not $state.tubeDesignerResult) {
                throw "TubeDesigner workflow result is missing."
            }
            if ([int]$state.tubeDesignerResult.dom.partCount -ne 0 -or
                @($state.tubeDesignerResult.designerState.parts).Count -ne 0) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner preview incorrectly created manufacturing parts: $designerText"
            }
            if (@($state.tubeDesignerResult.initialRibbonDom.ribbonCommandTitles).Count -ne 3 -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandTitles[0] -ne "添加" -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandTitles[1] -ne "批量添加" -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandTitles[2] -ne "导出加工" -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandIds[0] -ne "designer.add" -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandIds[1] -ne "designer.batch-add" -or
                $state.tubeDesignerResult.initialRibbonDom.ribbonCommandIds[2] -ne "designer.export-machining" -or
                [int]$state.tubeDesignerResult.initialRibbonDom.vectorRibbonIconCount -ne 3 -or
                [int]$state.tubeDesignerResult.initialRibbonDom.ribbonSplitToggleCount -ne 0 -or
                [int]$state.tubeDesignerResult.initialRibbonDom.visibleViewerHeaderCount -ne 0 -or
                -not $state.tubeDesignerResult.batchAddOperation.handled -or
                $state.tubeDesignerResult.batchAddState.batchImportPath -ne "D:\orders\security-windows.xlsx" -or
                -not $state.tubeDesignerResult.batchAddDom.notice.StartsWith("已选择 Excel 文件：")) {
                $designerText = @{
                    Titles = @($state.tubeDesignerResult.initialRibbonDom.ribbonCommandTitles)
                    CommandIds = @($state.tubeDesignerResult.initialRibbonDom.ribbonCommandIds)
                    VectorIconCount = [int]$state.tubeDesignerResult.initialRibbonDom.vectorRibbonIconCount
                    SplitToggles = [int]$state.tubeDesignerResult.initialRibbonDom.ribbonSplitToggleCount
                    ViewerHeaderCount = [int]$state.tubeDesignerResult.initialRibbonDom.visibleViewerHeaderCount
                    BatchAddOperation = $state.tubeDesignerResult.batchAddOperation
                    BatchAddState = $state.tubeDesignerResult.batchAddState
                    BatchAddNotice = $state.tubeDesignerResult.batchAddDom.notice
                } | ConvertTo-Json -Compress
                throw "TubeDesigner compact ribbon or batch-add contract failed: $designerText"
            }
            if ([int]$state.tubeDesignerResult.addDialogDom.addDialogCount -ne 1 -or
                [int]$state.tubeDesignerResult.addDialogDom.templateCardCount -lt 3 -or
                [int]$state.tubeDesignerResult.cancelledAddDom.addDialogCount -ne 0 -or
                [int]$state.tubeDesignerResult.dom.instanceCardCount -ne 1 -or
                [int]$state.tubeDesignerResult.dom.parameterFormCount -ne 1 -or
                @($state.tubeDesignerResult.designerState.instances).Count -ne 1) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner add dialog or instance workspace contract failed: $designerText"
            }
            if ([int]$state.tubeDesignerResult.generateBusyDom.operationProgressCount -ne 1 -or
                $state.tubeDesignerResult.generateBusyDom.operationProgressTitle -ne "正在生成产品预览" -or
                [int]$state.tubeDesignerResult.generateCompletedDom.operationProgressCount -ne 0) {
                $designerText = @{
                    Busy = $state.tubeDesignerResult.generateBusyDom
                    Completed = $state.tubeDesignerResult.generateCompletedDom
                } | ConvertTo-Json -Depth 8 -Compress
                throw "TubeDesigner generation progress did not follow operation lifetime: $designerText"
            }
            if ([int]$state.tubeDesignerResult.viewport.visibleObjectCount -ne 15) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner did not publish the expected automatic-spacing preview members: $designerText"
            }
            $previewDepth = [double]$state.tubeDesignerResult.viewport.contentBounds.max.y -
                [double]$state.tubeDesignerResult.viewport.contentBounds.min.y
            $previewHeight = [double]$state.tubeDesignerResult.viewport.contentBounds.max.z -
                [double]$state.tubeDesignerResult.viewport.contentBounds.min.z
            if ([Math]::Abs($previewDepth - 25.0) -gt 0.001 -or
                [Math]::Abs($previewHeight - 1800.0) -gt 0.001) {
                $designerText = $state.tubeDesignerResult.viewport.contentBounds | ConvertTo-Json -Depth 8 -Compress
                throw "TubeDesigner preview is not standing in the XZ plane: $designerText"
            }
            if ([int]$state.tubeDesignerResult.regeneratedDom.partCount -ne 0 -or
                [int]$state.tubeDesignerResult.regeneratedViewport.visibleObjectCount -ne 17) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner regeneration left stale or missing members: $designerText"
            }
            $parameterGroupsBefore = @($state.tubeDesignerResult.parameterPanelStateBeforeRegenerate.expandedGroups)
            $parameterGroupsAfter = @($state.tubeDesignerResult.parameterPanelStateAfterRegenerate.expandedGroups)
            if ([Math]::Abs(
                    [double]$state.tubeDesignerResult.parameterPanelStateBeforeRegenerate.scrollTop -
                    [double]$state.tubeDesignerResult.parameterPanelStateAfterRegenerate.scrollTop
                ) -gt 1 -or
                (@($parameterGroupsBefore) -join "|") -cne (@($parameterGroupsAfter) -join "|")) {
                $designerText = @{
                    Before = $state.tubeDesignerResult.parameterPanelStateBeforeRegenerate
                    After = $state.tubeDesignerResult.parameterPanelStateAfterRegenerate
                } | ConvertTo-Json -Depth 6 -Compress
                throw "TubeDesigner regeneration reset the parameter-panel editing context: $designerText"
            }
            $regeneratedWidth = [double]$state.tubeDesignerResult.regeneratedViewport.contentBounds.max.x -
                [double]$state.tubeDesignerResult.regeneratedViewport.contentBounds.min.x
            if ([Math]::Abs($regeneratedWidth - 1400.0) -gt 0.001) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner regeneration did not replace the preview geometry: $designerText"
            }
            if ([int]$state.tubeDesignerResult.undoResult.partCount -ne 0 -or
                [int]$state.tubeDesignerResult.redoResult.partCount -ne 0) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner undo/redo did not restore a coherent design: $designerText"
            }
            $designerStates = @(
                $state.tubeDesignerResult.designerState,
                $state.tubeDesignerResult.regeneratedDesignerState,
                $state.tubeDesignerResult.undoDesignerState,
                $state.tubeDesignerResult.redoDesignerState
            )
            if (@($designerStates | Where-Object { -not $_ }).Count -gt 0) {
                throw "TubeDesigner resource-version state capture is incomplete."
            }
            $designerMemberCounts = @(
                @($designerStates[0].members).Count,
                @($designerStates[1].members).Count,
                @($designerStates[2].members).Count,
                @($designerStates[3].members).Count
            )
            if ((@($designerMemberCounts) -join "|") -ne "15|17|15|17" -or
                @($designerStates | Where-Object { @($_.parts).Count -ne 0 }).Count -gt 0) {
                $designerText = $designerStates | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner resource-version state is incomplete: $designerText"
            }
            for ($index = 0; $index -lt 15; $index++) {
                $first = $designerStates[0].members[$index]
                $second = $designerStates[1].members[$index]
                $undo = $designerStates[2].members[$index]
                $redo = $designerStates[3].members[$index]
                if ($first.entityId -ne $second.entityId -or
                    $first.entityId -ne $undo.entityId -or
                    $first.entityId -ne $redo.entityId -or
                    $first.resourceId -ne $second.resourceId -or
                    $first.resourceId -ne $undo.resourceId -or
                    $first.resourceId -ne $redo.resourceId -or
                    [uint64]$first.resourceVersion -eq 0 -or
                    [uint64]$second.resourceVersion -le [uint64]$first.resourceVersion -or
                    [uint64]$undo.resourceVersion -ne [uint64]$first.resourceVersion -or
                    [uint64]$redo.resourceVersion -ne [uint64]$second.resourceVersion) {
                    $designerText = @($first, $second, $undo, $redo) |
                        ConvertTo-Json -Depth 8 -Compress
                    throw "TubeDesigner stable ID/version history failed for members[$index]: $designerText"
                }
            }
            $undoWidth = [double]$state.tubeDesignerResult.undoResult.viewport.contentBounds.max.x -
                [double]$state.tubeDesignerResult.undoResult.viewport.contentBounds.min.x
            $redoWidth = [double]$state.tubeDesignerResult.redoResult.viewport.contentBounds.max.x -
                [double]$state.tubeDesignerResult.redoResult.viewport.contentBounds.min.x
            if ([Math]::Abs($undoWidth - 1200.0) -gt 0.001 -or
                [Math]::Abs($redoWidth - 1400.0) -gt 0.001) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner View did not render the exact undo/redo resource versions: $designerText"
            }
            if ([Math]::Abs([double]$state.tubeDesignerResult.viewport.contentBounds.projectedCenter.x) -gt 1 -or
                [Math]::Abs([double]$state.tubeDesignerResult.viewport.contentBounds.projectedCenter.y) -gt 1) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner did not fit the generated product into the viewport: $designerText"
            }
            if ([int]$state.tubeDesignerResult.compatibilityTemplateCardCount -ne 0 -or
                [int]$state.tubeDesignerResult.disassemblySelectionDom.disassemblyDialogCount -ne 1 -or
                [int]$state.tubeDesignerResult.disassemblySelectionDom.disassemblyInstanceRowCount -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount -or
                $state.tubeDesignerResult.disassemblySelectionDom.selectionTableBorderCollapse -ne "collapse" -or
                [int]$state.tubeDesignerResult.disassemblySelectionDom.nonNativeTableCellCount -ne 0 -or
                [int]$state.tubeDesignerResult.breakdownDom.breakdownDialogCount -ne 1 -or
                [int]$state.tubeDesignerResult.breakdownDom.productGroupCount -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount -or
                [int]$state.tubeDesignerResult.breakdownDom.categoryRowCount -le 0 -or
                [int]$state.tubeDesignerResult.breakdownDom.visiblePartRowCount -ne 0 -or
                $state.tubeDesignerResult.breakdownDom.resultTableBorderCollapse -ne "collapse" -or
                [int]$state.tubeDesignerResult.breakdownDom.resultTableColumnCount -ne 8 -or
                [int]$state.tubeDesignerResult.breakdownDom.nonNativeTableCellCount -ne 0 -or
                @($state.tubeDesignerResult.breakdownDom.productRowSpans).Count -ne 0 -or
                [int]$state.tubeDesignerResult.breakdownDom.partCount -ne
                    [int]$state.tubeDesignerResult.expectedDisassembledPartCount -or
                [int]$state.tubeDesignerResult.breakdownDom.selectedPartCount -ne
                    [int]$state.tubeDesignerResult.expectedDisassembledPartCount -or
                [int]$state.tubeDesignerResult.breakdownDom.resourceThumbnailCount -ne
                    [int]$state.tubeDesignerResult.breakdownDom.categoryRowCount -or
                [int]$state.tubeDesignerResult.breakdownDom.breakdownFooterCloseButtonCount -ne 0 -or
                @($state.tubeDesignerResult.breakdownDom.partNames | Where-Object { [string]::IsNullOrWhiteSpace($_) -or $_ -eq "main" }).Count -ne 0 -or
                @($state.tubeDesignerResult.breakdownDom.partSpecifications | Where-Object { $_ -eq "—" -or $_ -match "0\s*×\s*0" }).Count -ne 0 -or
                @($state.tubeDesignerResult.disassembledDesignerState.manufacturingGroups).Count -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount) {
                $designerText = @{
                    DisassemblySelection = $state.tubeDesignerResult.disassemblySelectionDom
                    Breakdown = $state.tubeDesignerResult.breakdownDom
                    ManufacturingGroupCount = @($state.tubeDesignerResult.disassembledDesignerState.manufacturingGroups).Count
                } | ConvertTo-Json -Depth 12 -Compress
                throw "TubeDesigner grouped disassembly workflow is incomplete: $designerText"
            }
            $pythonTemplateParts = @(
                $state.tubeDesignerResult.disassembledDesignerState.manufacturingGroups |
                    Where-Object templateId -eq "single-face-security-window" |
                    ForEach-Object { @($_.parts) }
            )
            $invalidPythonPartNumbers = @($pythonTemplateParts | Where-Object {
                $partNumber = [string]$_.partNumber
                $fileStem = [IO.Path]::GetFileNameWithoutExtension([string]$_.fileName)
                [string]::IsNullOrWhiteSpace($partNumber) -or
                    $fileStem -cne $partNumber -or
                    $partNumber -notmatch '.+-.+-\d{2}(-段\d{2})?$'
            })
            if ($pythonTemplateParts.Count -eq 0 -or $invalidPythonPartNumbers.Count -gt 0) {
                $designerText = @{
                    PythonPartCount = $pythonTemplateParts.Count
                    InvalidParts = $invalidPythonPartNumbers
                } | ConvertTo-Json -Depth 8 -Compress
                throw "TubeDesigner formal part-number/STEP filename contract failed: $designerText"
            }
            if ([int]$state.tubeDesignerResult.partialSelectionDom.selectedPartCount -ne
                ([int]$state.tubeDesignerResult.expectedDisassembledPartCount - 1)) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner partial manufacturing selection failed: $designerText"
            }
            if ([int]$state.tubeDesignerResult.partInspectionDom.partInspectionDialogCount -ne 1 -or
                [int]$state.tubeDesignerResult.partInspectionDom.partInspectionCanvasCount -ne 1 -or
                $state.tubeDesignerResult.partInspectionDom.partInspectionReady -ne "true" -or
                [int]$state.tubeDesignerResult.partInspectionDom.partInspectionEntityCount -ne 1 -or
                [int]$state.tubeDesignerResult.partInspectionDom.partInspectionDimensionCount -lt 1 -or
                [int]$state.tubeDesignerResult.partInspectionProgressDom.partInspectionProgressCount -ne 1 -or
                $state.tubeDesignerResult.partInspectionProgressDom.partInspectionProgressVisible -ne $true -or
                $state.tubeDesignerResult.partInspectionProgressDom.partInspectionProgressAriaHidden -ne "false" -or
                [int]$state.tubeDesignerResult.partInspectionDom.partInspectionProgressMs -lt 500 -or
                $state.tubeDesignerResult.partInspectionDom.partInspectionProgressVisible -ne $false -or
                $state.tubeDesignerResult.partInspectionDom.partInspectionProgressAriaHidden -ne "true" -or
                $state.tubeDesignerResult.partInspectionDom.partInspectionRenderMode -ne "on-demand" -or
                $state.tubeDesignerResult.partInspectionDom.partInspectionPickingEnabled -ne "false" -or
                [int]$state.tubeDesignerResult.partInspectionDom.breakdownDialogCount -ne 0 -or
                [int]$state.tubeDesignerResult.closedPartInspectionDom.partInspectionDialogCount -ne 0 -or
                [int]$state.tubeDesignerResult.closedPartInspectionDom.breakdownDialogCount -ne 1) {
                $designerText = @{
                    Inspection = $state.tubeDesignerResult.partInspectionDom
                    Closed = $state.tubeDesignerResult.closedPartInspectionDom
                } | ConvertTo-Json -Depth 10 -Compress
                throw "TubeDesigner isolated part inspection workflow failed: $designerText"
            }
            if ($state.tubeDesignerResult.partialSelectionPreservedDom -ne $true) {
                throw "TubeDesigner part selection rebuilt the breakdown dialog instead of updating it locally"
            }
            if ($CheckTubeDesignerStepExport -and
                ([int]$state.tubeDesignerResult.exportResult.result.exportedCount -ne
                    ([int]$state.tubeDesignerResult.expectedDisassembledPartCount - 1) -or
                 @($state.tubeDesignerResult.exportResult.result.exportedGroups).Count -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount -or
                 [string]::IsNullOrWhiteSpace($state.tubeDesignerResult.exportResult.result.partListFile))) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner grouped STEP and Excel export failed: $designerText"
            }
            if ($CheckTubeDesignerStepExport -and
                ([int]$state.tubeDesignerResult.exportBusyDom.exportProgressCount -ne 1 -or
                 $state.tubeDesignerResult.exportBusyDom.breakdownDialogBusy -ne "true" -or
                 $state.tubeDesignerResult.exportBusyDom.exportButtonDisabled -ne $true -or
                 [int]$state.tubeDesignerResult.exportLockedDom.breakdownDialogCount -ne 1 -or
                 [int]$state.tubeDesignerResult.exportLockedDom.exportProgressCount -ne 1 -or
                 $null -ne $state.tubeDesignerResult.duplicateExportResult.result -or
                 [int]$state.tubeDesignerResult.exportCompletedDom.exportProgressCount -ne 0 -or
                 $state.tubeDesignerResult.exportCompletedDom.breakdownDialogBusy -ne "false")) {
                $designerText = @{
                    Busy = $state.tubeDesignerResult.exportBusyDom
                    Locked = $state.tubeDesignerResult.exportLockedDom
                    Completed = $state.tubeDesignerResult.exportCompletedDom
                    Duplicate = $state.tubeDesignerResult.duplicateExportResult
                } | ConvertTo-Json -Depth 10 -Compress
                throw "TubeDesigner export waiting/locking state is incomplete: $designerText"
            }
            if (@($state.tubeDesignerResult.undoDisassemblyDesignerState.manufacturingGroups).Count -ne 0 -or
                [int]$state.tubeDesignerResult.undoDisassemblyResult.viewport.visibleObjectCount -ne 24 -or
                @($state.tubeDesignerResult.redoDisassemblyDesignerState.manufacturingGroups).Count -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount -or
                [int]$state.tubeDesignerResult.redoDisassemblyResult.viewport.visibleObjectCount -ne 24) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner disassembly undo/redo incorrectly synchronized preview state: $designerText"
            }
            for ($index = 0; $index -lt @($state.tubeDesignerResult.disassembledDesignerState.parts).Count; $index++) {
                $before = $state.tubeDesignerResult.disassembledDesignerState.parts[$index]
                $redo = $state.tubeDesignerResult.redoDisassemblyDesignerState.parts[$index]
                if ($before.entityId -ne $redo.entityId -or
                    $before.resourceId -ne $redo.resourceId -or
                    [uint64]$before.resourceVersion -ne [uint64]$redo.resourceVersion) {
                    $designerText = @($before, $redo) | ConvertTo-Json -Depth 8 -Compress
                    throw "TubeDesigner disassembly history changed part identity/version at index ${index}: $designerText"
                }
            }
            if ($state.tubeDesignerResult.accessDoorDesignerState.product.templateId -ne "single-face-security-window" -or
                $state.tubeDesignerResult.accessDoorDesignerState.product.parameters.accessDoorEnabled -ne $true -or
                @($state.tubeDesignerResult.accessDoorDesignerState.members).Count -ne 24 -or
                @($state.tubeDesignerResult.accessDoorDesignerState.members | Where-Object { $_.stableKey -like "*.continuous.*" }).Count -ne 2 -or
                @($state.tubeDesignerResult.accessDoorDesignerState.members | Where-Object role -eq "access_door.fixed_frame").Count -ne 4 -or
                @($state.tubeDesignerResult.accessDoorDesignerState.instances).Count -ne 2 -or
                [int]$state.tubeDesignerResult.accessDoorViewport.visibleObjectCount -ne 24 -or
                [int]$state.tubeDesignerResult.accessDoorInspectorDom.parameterSectionCount -ne 19 -or
                [int]$state.tubeDesignerResult.accessDoorInspectorDom.expandedParameterSectionCount -ne 2 -or
                [int]$state.tubeDesignerResult.accessDoorInspectorDom.visibleParameterFieldCount -ne 5 -or
                [double]$state.tubeDesignerResult.accessDoorInspectorDom.maximumVisibleParameterFieldHeight -gt 32 -or
                $state.tubeDesignerResult.accessDoorInspectorDom.parameterSectionOverflowY -ne "auto" -or
                [int]$state.tubeDesignerResult.accessDoorInspectorDom.parameterActionButtonCount -ne 1 -or
                $state.tubeDesignerResult.accessDoorInspectorDom.parameterHeaderInsideScrollArea -ne $false -or
                $state.tubeDesignerResult.accessDoorInspectorDom.parameterPanelOverflowY -ne "hidden" -or
                [int]$state.tubeDesignerResult.accessDoorInspectorDom.parameterDisassemblyButtonCount -ne 0 -or
                $state.tubeDesignerResult.accessDoorParameterDom.doorVerticalType -ne "round" -or
                (@($state.tubeDesignerResult.accessDoorParameterDom.profileTypeLabels) -join "|") -ne "矩形管|矩形管|圆管|矩形管|矩形管|矩形管|圆管" -or
                $state.tubeDesignerResult.accessDoorParameterDom.outerProcess -ne "v_groove_90:left_arc" -or
                [int]$state.tubeDesignerResult.accessDoorParameterDom.outerProcessCount -ne 6 -or
                $state.tubeDesignerResult.accessDoorParameterDom.fixedProcess -ne "miter_45" -or
                [int]$state.tubeDesignerResult.accessDoorParameterDom.fixedProcessCount -ne 6 -or
                $state.tubeDesignerResult.accessDoorParameterDom.leafProcess -ne "v_groove_90:rounded_v" -or
                [int]$state.tubeDesignerResult.accessDoorParameterDom.leafProcessCount -ne 6) {
                $designerText = @{
                    Designer = $state.tubeDesignerResult.accessDoorDesignerState
                    Inspector = $state.tubeDesignerResult.accessDoorInspectorDom
                    Parameters = $state.tubeDesignerResult.accessDoorParameterDom
                    VisibleObjectCount = $state.tubeDesignerResult.accessDoorViewport.visibleObjectCount
                } | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner access-door preview workflow failed: $designerText"
            }
            if ($state.tubeDesignerResult.finalAccessDoorState.product.entityId -ne $state.tubeDesignerResult.accessDoorProductId -or
                @($state.tubeDesignerResult.finalAccessDoorState.members).Count -ne 24 -or
                [int]$state.tubeDesignerResult.finalAccessDoorViewport.visibleObjectCount -ne 24) {
                $designerText = $state.tubeDesignerResult.finalAccessDoorState | ConvertTo-Json -Depth 12 -Compress
                throw "TubeDesigner final access-door activation failed: $designerText"
            }
            $multiFaceExpectations = @(
                [pscustomobject]@{ Result = $state.tubeDesignerResult.twoFaceTemplateResult; TemplateId = "two-face-security-window"; MemberCount = 28; InstanceCount = 3 },
                [pscustomobject]@{ Result = $state.tubeDesignerResult.threeFaceTemplateResult; TemplateId = "three-face-security-window"; MemberCount = 39; InstanceCount = 4 },
                [pscustomobject]@{ Result = $state.tubeDesignerResult.fiveFaceTemplateResult; TemplateId = "five-face-security-window"; MemberCount = 63; InstanceCount = 5 }
            )
            foreach ($expectation in $multiFaceExpectations) {
                $result = $expectation.Result
                $templateId = [string]$expectation.TemplateId
                $memberCount = [int]$expectation.MemberCount
                $instanceCount = [int]$expectation.InstanceCount
                if ($result.state.product.templateId -ne $templateId -or
                    @($result.state.members).Count -ne $memberCount -or
                    [int]$result.viewport.visibleObjectCount -ne $memberCount -or
                    @($result.state.instances).Count -ne $instanceCount) {
                    $designerText = $result | ConvertTo-Json -Depth 14 -Compress
                    throw "TubeDesigner multi-face Python template failed (${templateId}): $designerText"
                }
            }
            $stairResult = $state.tubeDesignerResult.stairTemplateResult
            if (-not $stairResult.operation.handled -or
                $stairResult.state.product.templateId -ne "straight-stair-railing" -or
                $stairResult.state.product.parameters.infillType -ne "vertical" -or
                @($stairResult.state.members).Count -ne 29 -or
                @($stairResult.state.instances).Count -ne 6 -or
                [int]$stairResult.viewport.visibleObjectCount -ne 29 -or
                [int]$stairResult.industryGroupCount -lt 2) {
                $designerText = $stairResult | ConvertTo-Json -Depth 14 -Compress
                throw "TubeDesigner stair industry template failed: $designerText"
            }
            if ($state.tubeDesignerResult.firstActivatedState.product.entityId -ne $state.tubeDesignerResult.firstProductId -or
                @($state.tubeDesignerResult.firstActivatedState.members).Count -ne 17 -or
                [int]$state.tubeDesignerResult.firstActivatedViewport.visibleObjectCount -ne 17 -or
                $state.tubeDesignerResult.secondActivatedState.product.entityId -ne $state.tubeDesignerResult.secondProductId -or
                @($state.tubeDesignerResult.secondActivatedState.members).Count -ne 24 -or
                [int]$state.tubeDesignerResult.secondActivatedViewport.visibleObjectCount -ne 24) {
                $designerText = $state.tubeDesignerResult | ConvertTo-Json -Depth 16 -Compress
                throw "TubeDesigner instance selection did not exclusively drive the scene: $designerText"
            }
            if (-not $state.tubeDesignerResult.firstActivationNoticeDom.notice.StartsWith("已切换到 ") -or
                [double]$state.tubeDesignerResult.firstActivationNoticeDom.noticeLayout.centerOffset -gt 1.0 -or
                [double]$state.tubeDesignerResult.firstActivationNoticeDom.noticeLayout.top -lt 0 -or
                $state.tubeDesignerResult.firstActivationNoticeDom.noticeLayout.pointerEvents -ne "none" -or
                $state.tubeDesignerResult.dismissedActivationNoticeDom.notice -ne "") {
                $designerText = @{
                    Visible = $state.tubeDesignerResult.firstActivationNoticeDom
                    Dismissed = $state.tubeDesignerResult.dismissedActivationNoticeDom
                } | ConvertTo-Json -Depth 8 -Compress
                throw "TubeDesigner activation notice placement or dismissal failed: $designerText"
            }
        }
        if ($CheckTubeDesignerStepExport) {
            $stepFiles = @(Get-ChildItem -LiteralPath $tubeDesignerExportDirectory -Filter "*.step" -File -Recurse -ErrorAction SilentlyContinue)
            $productDirectories = @(Get-ChildItem -LiteralPath $tubeDesignerExportDirectory -Directory -ErrorAction SilentlyContinue)
            $partListFile = Join-Path $tubeDesignerExportDirectory "零件清单.xlsx"
            if ($stepFiles.Count -le 0 -or $productDirectories.Count -ne
                    [int]$state.tubeDesignerResult.expectedDisassemblyProductCount -or
                -not (Test-Path -LiteralPath $partListFile -PathType Leaf) -or
                (Get-Item -LiteralPath $partListFile).Length -le 0 -or
                @($stepFiles | Where-Object Length -le 0).Count -gt 0 -or
                @($productDirectories | Where-Object { @(Get-ChildItem -LiteralPath $_.FullName -Filter "*.step" -File).Count -eq 0 }).Count -gt 0) {
                $fileText = @{ Directories = $productDirectories.Name; Files = $stepFiles | Select-Object Name, Length; PartList = $partListFile } | ConvertTo-Json -Depth 8 -Compress
                throw "TubeDesigner did not export the selected product folders, STEP files, and Excel part list: $fileText"
            }
        }

        if ($ScreenshotPath) {
            $screenshot = Invoke-CdpCommand -Socket $socket -Id 2 -Method "Page.captureScreenshot" -Params @{
                format = "png"
                fromSurface = $true
                captureBeyondViewport = $false
            }
            $targetPath = [IO.Path]::GetFullPath($ScreenshotPath)
            $targetDirectory = Split-Path -Parent $targetPath
            if ($targetDirectory) {
                New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
            }
            [IO.File]::WriteAllBytes($targetPath, [Convert]::FromBase64String($screenshot.result.data))
        }

        if ($RequireRenderable) {
            if (-not $state.productDiagnosticsReady) {
                throw "Laser3DCAM frontend diagnostics are not ready."
            }
            if (-not $state.viewport) {
                throw "Viewport debug state is missing."
            }
            if ([int]$state.viewport.canvasClientWidth -le 0 -or [int]$state.viewport.canvasClientHeight -le 0) {
                throw "Viewport canvas has no layout size."
            }
            if ([int]$state.viewport.geometryCount -le 0) {
                throw "Viewport has no geometry."
            }
            if ([int]$state.viewport.objectCount -le 0) {
                throw "Viewport has no objects."
            }
            if ([int]$state.viewport.visibleObjectCount -le 0) {
                throw "Viewport has no visible objects."
            }
            if (-not $state.viewport.cameraPosition -or -not $state.viewport.cameraDirection) {
                throw "Viewport has no local camera state."
            }
            if ($state.viewport.pixelSample -and [int]$state.viewport.pixelSample.nonBackground -le 0) {
                throw "Viewport rendered only background pixels."
            }
        }

        if ($CheckTubeCSGWorkflow) {
            if (-not $state.product.tubeGeometry.available) {
                throw "Tube neutral geometry is unavailable."
            }
            if ([int]$state.product.tubeGeometry.solidNodeCount -le 0) {
                throw "Tube manufacturing model has no feature nodes."
            }
            if (-not $state.tubeMainDomState) {
                throw "TubesT-style TubeOne main workspace diagnostics are missing."
            }
            if (-not $state.tubeCADIntentDomState) {
                throw "Tube CAD intent DOM diagnostics are missing."
            }
            if ([int]$state.tubeMainDomState.mainWorkspaceCount -ne 1 -or [int]$state.tubeMainDomState.bottomDockCount -ne 1 -or [int]$state.tubeMainDomState.layerStripCount -ne 1 -or [int]$state.tubeMainDomState.visibleVectorRibbonIconCount -le 0) {
                throw "TubesT-style TubeOne main workspace is incomplete."
            }
            if ([int]$state.tubeCADIntentDomState.editorWorkspaceCount -ne 1 -or [int]$state.tubeCADIntentDomState.manufacturingTreeCount -ne 1 -or [int]$state.tubeCADIntentDomState.layerStripCount -ne 0 -or [int]$state.tubeCADIntentDomState.visibleVectorRibbonIconCount -le 0) {
                throw "Tube three-dimensional part editor is incomplete."
            }
            if ([int]$state.tubeCADIntentDomState.editorDialogCount -ne 1) {
                throw "Tube CAD editor is not hosted by exactly one modal dialog."
            }
            if (-not $state.tubeCADIntentDomState.editorSceneId -or [string]$state.tubeCADIntentDomState.editorSceneId -ne [string]$state.tubeCADIntentDomState.editorSceneProxyId) {
                throw "Tube CAD editor did not connect to its independent Scene proxy."
            }
            if (-not $state.tubeCADIntentDomState.mainSceneId -or [string]$state.tubeCADIntentDomState.editorSceneId -eq [string]$state.tubeCADIntentDomState.mainSceneId) {
                throw "Tube CAD editor reused the main Scene instead of opening an independent child Scene."
            }
            if ([int]$state.tubeCADIntentDomState.sectionPrimitiveCount -lt 3) {
                throw "Tube CAD editor did not expose the stock outer profile and cavities as section primitives."
            }
            if (-not $state.tubeCADIntentDomState.viewportLayout.hostMatchesLiveElement) {
                throw "Tube CAD editor viewport is not mounted in the live independent-Scene dialog."
            }
            if ([int]$state.tubeCADIntentDomState.historyNodeCount -le 0) {
                throw "Tube history tree is empty."
            }
            if ([int]$state.tubeMainDomState.workpieceCardCount -le 0 -or [int]$state.tubeMainDomState.activeWorkpieceCardCount -ne 1) {
                throw "Tube workpiece card list is missing or has no unique active card."
            }
            if ([int]$state.tubeMainDomState.workpieceThumbnailCount -ne [int]$state.tubeMainDomState.workpieceCardCount) {
                throw "Tube workpiece cards do not all contain thumbnails."
            }
            if ([int]$state.tubeMainDomState.resourceWorkpieceThumbnailCount -ne [int]$state.tubeMainDomState.workpieceCardCount) {
                throw "Tube workpiece cards did not render their resource-backed previews."
            }
            if ([int]$state.tubeMainDomState.workpiecePathInputCount -ne 0) {
                throw "Tube workpiece panel still exposes a source path input."
            }
            if ([int]$state.tubeMainDomState.addWorkpieceButtonCount -lt 1 -or [int]$state.tubeMainDomState.deleteWorkpieceButtonCount -ne 1) {
                throw "Tube workpiece add/delete toolbar is incomplete."
            }
            if ([int]$state.tubeMainDomState.legacyWorkpieceRibbonCommandCount -ne 0) {
                throw "Tube workpiece add/delete actions are still duplicated in the ribbon."
            }
            if ([int]$state.tubeMainDomState.mainPropertyPaneCount -ne 1 `
                -or [int]$state.tubeMainDomState.workpiecePaginationCount -ne 1 `
                -or [int]$state.tubeMainDomState.nestingResultListCount -ne 1 `
                -or [int]$state.tubeMainDomState.bottomTabButtonCount -ne 2 `
                -or [int]$state.tubeMainDomState.bottomSplitterCount -ne 1) {
                throw "Tube main workspace does not match the part/scene/property/tabbed-result layout."
            }
            if (-not $state.tubeBottomResizeResult `
                -or [double]$state.tubeBottomResizeResult.afterLeftWidth -le [double]$state.tubeBottomResizeResult.beforeLeftWidth `
                -or [double]$state.tubeBottomResizeResult.afterRightWidth -le [double]$state.tubeBottomResizeResult.beforeRightWidth `
                -or [double]$state.tubeBottomResizeResult.afterBottomHeight -le [double]$state.tubeBottomResizeResult.beforeBottomHeight) {
                throw "Tube main workspace regions did not grow after dragging their splitters."
            }
            if (-not $state.tubeToolbarTooltipResult `
                -or [int]$state.tubeToolbarTooltipResult.shownTooltip.visibleToolbarTooltipCount -ne 1 `
                -or -not [string]$state.tubeToolbarTooltipResult.shownTooltip.toolbarTooltipText `
                -or [int]$state.tubeToolbarTooltipResult.hiddenTooltip.visibleToolbarTooltipCount -ne 0) {
                throw "Tube toolbar icon bubble tooltip did not show and hide correctly."
            }
            if (-not $state.tubeWorkpieceSearchResult `
                -or [int]$state.tubeWorkpieceSearchResult.byName.visibleWorkpieceCardCount -ne 1 `
                -or [int]$state.tubeWorkpieceSearchResult.byProcess.visibleWorkpieceCardCount -ne 1 `
                -or [int]$state.tubeWorkpieceSearchResult.excludedProcess.visibleWorkpieceCardCount -ne 0 `
                -or [int]$state.tubeWorkpieceSearchResult.booleanOr.visibleWorkpieceCardCount -ne 1 `
                -or -not $state.tubeWorkpieceSearchResult.invalidFilter.workpieceSearchInvalid `
                -or [int]$state.tubeWorkpieceSearchResult.cleared.visibleWorkpieceCardCount -ne 1) {
                throw "Tube Unity-style n:/t: workpiece query workflow is incomplete."
            }
            if (-not $state.tubeFilteredSelectionResult `
                -or [int]$state.tubeFilteredSelectionResult.selectedAll.selectedWorkpieceCardCount -ne 1 `
                -or [int]$state.tubeFilteredSelectionResult.inverted.selectedWorkpieceCardCount -ne 0) {
                throw "Tube filtered all/invert selection workflow is incomplete."
            }
            if (-not $state.tubeBottomTabResult `
                -or [string]$state.tubeBottomTabResult.stock.activeBottomTab -ne "stock" `
                -or [int]$state.tubeBottomTabResult.stock.stockBottomPanelCount -ne 1 `
                -or [int]$state.tubeBottomTabResult.stock.nestingBottomPanelCount -ne 0 `
                -or [string]$state.tubeBottomTabResult.nesting.activeBottomTab -ne "nesting" `
                -or [int]$state.tubeBottomTabResult.nesting.nestingBottomPanelCount -ne 1 `
                -or [int]$state.tubeBottomTabResult.nesting.stockBottomPanelCount -ne 0) {
                throw "Tube bottom tabs do not switch between nesting results and stock."
            }
            if ([int]$state.tubeMainDomState.visibleMainViewCubeCount -ne 1 `
                -or [int]$state.tubeMainDomState.visibleMainAxisCount -ne 1) {
                throw "Tube main viewport is missing the ViewCube or coordinate system."
            }
            if (@($state.tubeMainDomState.workpieceQuantityLabels).Count -ne [int]$state.tubeMainDomState.workpieceCardCount) {
                throw "Tube workpiece cards do not all expose quantity."
            }
            if ([int]$state.tubeCADIntentDomState.selectedHistoryNodeCount -ne 1) {
                throw "Tube history tree does not have exactly one selected node."
            }
            if ([int]$state.tubeCADIntentDomState.parameterCount -le 0) {
                throw "Tube selected node exposes no parameters."
            }
            if (-not $state.tubeCADIntentDomState.previewActive) {
                throw "Tube selected node ghost preview is not active."
            }
            if ([int]$state.tubePreviewResults.Count -le 0) {
                throw "Tube manufacturing-feature tree has no previewable nodes."
            }
            foreach ($preview in @($state.tubePreviewResults)) {
                if ([string]$preview.nodeId -ne [string]$preview.selectedNodeId -or [string]$preview.nodeId -ne [string]$preview.previewNodeId) {
                    throw "Tube history selection did not activate the requested ghost preview: $($preview.nodeId)"
                }
                if (-not $preview.ghostPreviewVisible -or [int]$preview.ghostPreviewVertexCount -le 0) {
                    throw "Tube history node has no renderable ghost preview: $($preview.nodeId)"
                }
            }
            if (-not $state.tubeParameterEditResult) {
                throw "Tube selected node has no editable parameter workflow."
            }
            if ($state.tubeParameterEditResult.changed.error -or $state.tubeParameterEditResult.reverted.error) {
                throw "Tube parameter update reported an error."
            }
            $livePreview = $state.tubeParameterEditResult.livePreview
            if (-not $livePreview -or -not $livePreview.livePreviewResourceUrl) {
                throw "Tube parameter input did not publish a live construction-body preview."
            }
            if ([string]$livePreview.livePreviewNodeId -ne [string]$livePreview.nodeId `
                -or -not $livePreview.ghostPreviewVisible `
                -or [int]$livePreview.ghostPreviewVertexCount -le 0) {
                throw "Tube parameter input did not display the changed tool as a ghost preview."
            }
            if ([int]$livePreview.afterVersion -ne [int]$livePreview.beforeVersion `
                -or [int]$livePreview.afterGeometryRevision -ne [int]$livePreview.beforeGeometryRevision `
                -or [string]$livePreview.afterBRepResourceId -ne [string]$livePreview.beforeBRepResourceId) {
                throw "Tube live preview changed the committed part before Apply."
            }
            if ([int]$state.tubeParameterEditResult.changed.afterVersion -le [int]$state.tubeParameterEditResult.changed.beforeVersion) {
                throw "Tube parameter update did not publish a new CSG version."
            }
            foreach ($edit in @($state.tubeParameterEditResult.changed, $state.tubeParameterEditResult.reverted)) {
                if ([int]$edit.afterGeometryRevision -le [int]$edit.beforeGeometryRevision) {
                    throw "Tube parameter update did not increment the workpiece geometry revision."
                }
                if (-not $edit.afterBRepResourceId -or [string]$edit.afterBRepResourceId -eq [string]$edit.beforeBRepResourceId) {
                    throw "Tube parameter update did not publish a new evaluated BRep resource."
                }
                if (-not $edit.afterRenderGeometryResourceUrl -or [string]$edit.afterRenderGeometryResourceUrl -eq [string]$edit.beforeRenderGeometryResourceUrl) {
                    throw "Tube parameter update did not replace the scene render geometry resource."
                }
                $previewChanged = [string]$edit.afterPreviewResourceUrl -ne [string]$edit.beforePreviewResourceUrl `
                    -or [int]$edit.afterPreviewResourceVersion -ne [int]$edit.beforePreviewResourceVersion
                if (-not $edit.afterPreviewResourceUrl -or -not $previewChanged) {
                    throw "Tube parameter update did not regenerate the selected-node construction preview."
                }
            }
            if ([double]$state.tubeParameterEditResult.changed.value -ne [double]$state.tubeParameterEditResult.changedValue) {
                throw "Tube parameter update did not persist the requested value: expected $($state.tubeParameterEditResult.changedValue), actual $($state.tubeParameterEditResult.changed.value)."
            }
            if ([double]$state.tubeParameterEditResult.reverted.value -ne [double]$state.tubeParameterEditResult.originalValue) {
                throw "Tube parameter update did not restore the original value: expected $($state.tubeParameterEditResult.originalValue), actual $($state.tubeParameterEditResult.reverted.value)."
            }
            if (-not $state.viewport.ghostPreviewVisible -or [int]$state.viewport.ghostPreviewVertexCount -le 0) {
                throw "Tube ghost preview has no renderable geometry."
            }
        }

        if ($CheckDefaultMachine) {
            if (-not $state.defaultMachineResult) {
                throw "Default machine result is missing."
            }
            if ([int]$state.defaultMachineResult.machineDefinitionCount -le 0) {
                throw "Default machine definition was not created."
            }
            if ([int]$state.defaultMachineResult.machineInstanceCount -le 0) {
                throw "Default machine instance was not created."
            }
            if ([int]$state.defaultMachineResult.enabledMachineInstanceCount -le 0) {
                throw "Default machine instance is not enabled."
            }
        }

        if ($CheckMachineEnableWorkflow) {
            if (-not $state.machineEnableResult) {
                throw "Machine enable workflow result is missing."
            }
            $afterDisable = $state.machineEnableResult.afterDisable
            $afterEnable = $state.machineEnableResult.afterEnable
            if ([int]$afterDisable.enabledMachineInstanceCount -ne 0) {
                throw "Disabled machine instance is still counted as a job candidate."
            }
            if ($afterDisable.jobMachineId) {
                throw "Disabled machine instance is still assigned to the job."
            }
            if ([int]$afterEnable.enabledMachineInstanceCount -le 0) {
                throw "Re-enabled machine instance was not restored as a job candidate."
            }
        }

        if ($CheckMachineRenameWorkflow) {
            if (-not $state.machineRenameResult) {
                throw "Machine rename workflow result is missing."
            }
            $renamed = $false
            foreach ($machine in @($state.machineRenameResult.afterRename.machineInstances)) {
                if ([string]$machine.id -eq [string]$state.machineRenameResult.machineId -and [string]$machine.name -eq [string]$state.machineRenameResult.targetName) {
                    $renamed = $true
                }
            }
            if (-not $renamed) {
                $renameText = $state.machineRenameResult | ConvertTo-Json -Depth 16 -Compress
                throw "Machine rename workflow did not update the instance name: $renameText"
            }
        }

        if ($CheckMachineSelectionWorkflow) {
            if (-not $state.machineSelectionResult) {
                throw "Machine selection workflow result is missing."
            }
            if (-not $state.machineSelectionResult.result -or -not $state.machineSelectionResult.result.ok) {
                $selectionText = $state.machineSelectionResult | ConvertTo-Json -Depth 16 -Compress
                throw "Machine selection workflow failed: $selectionText"
            }
            if ($state.machineSelectionResult.state.error) {
                throw "Machine selection workflow left frontend error: $($state.machineSelectionResult.state.error)"
            }
            if ([string]$state.machineSelectionResult.viewport.selectedObjectId -ne [string]$state.machineSelectionResult.expectedObjectId) {
                $selectionText = $state.machineSelectionResult | ConvertTo-Json -Depth 16 -Compress
                throw "Viewport did not highlight the selected machine object: $selectionText"
            }
            if (-not $state.machineSelectionResult.domAfterPick -or -not $state.machineSelectionResult.domAfterPick.hasAppearanceEditor) {
                throw "Machine element appearance editor was not shown after selection."
            }
            if (-not $state.machineSelectionResult.domAfterPick.hasCollisionToggle) {
                throw "Machine element collision toggle was not shown after selection."
            }
            if ([int]$state.machineSelectionResult.domAfterPick.selectedTreeRows -le 0) {
                throw "Machine tree did not sync selection after viewport pick."
            }
            if (-not $state.machineSelectionResult.appearance -or -not $state.machineSelectionResult.appearance.result -or -not $state.machineSelectionResult.appearance.result.ok) {
                $appearanceText = $state.machineSelectionResult.appearance | ConvertTo-Json -Depth 16 -Compress
                throw "Machine appearance workflow failed: $appearanceText"
            }
            if (-not $state.machineSelectionResult.collider) {
                throw "Machine collider PDO wire state was not observed after enabling collision display."
            }
            if ([int]$state.machineSelectionResult.collider.visibleColliderObjectCount -le 0) {
                $colliderText = $state.machineSelectionResult.collider | ConvertTo-Json -Depth 16 -Compress
                throw "Machine collider PDO produced no visible wire objects: $colliderText"
            }
            if ([int]$state.machineSelectionResult.collider.colliderShapeCount -le 0) {
                $colliderText = $state.machineSelectionResult.collider | ConvertTo-Json -Depth 16 -Compress
                throw "Machine collider PDO produced no primitive shapes: $colliderText"
            }
            if (-not $state.machineSelectionResult.standardView -or -not $state.machineSelectionResult.standardView.result) {
                $viewText = $state.machineSelectionResult.standardView | ConvertTo-Json -Depth 16 -Compress
                throw "Machine standard view workflow failed: $viewText"
            }
            if (-not $state.machineDomState -or -not $state.machineDomState.hasViewCube) {
                throw "Machine viewport view cube is missing."
            }
            if (-not $state.machineDomState.viewCubePitch -or -not $state.machineDomState.viewCubeYaw) {
                $viewCubeText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport view cube did not synchronize camera orientation: $viewCubeText"
            }
            if (-not $state.machineDomState.hasAxisGizmo) {
                throw "Machine viewport axis gizmo is missing."
            }
            if ($state.machineDomState.hasViewCubeEmbeddedAxis) {
                throw "Machine viewport view cube still embeds the world axis gizmo."
            }
            if ([int]$state.machineDomState.viewCubePieceCount -ne 26) {
                $viewCubeText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport view cube is not assembled from 26 clickable pieces: $viewCubeText"
            }
            if ([int]$state.machineDomState.viewCubeCanvasCount -ne 0) {
                $viewCubeText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport view cube must not allocate its own WebGL canvas: $viewCubeText"
            }
            if ([int]$state.machineDomState.viewCubeCellCount -ne 0) {
                $viewCubeText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport view cube still uses 3x3 cell hot zones: $viewCubeText"
            }
            if ([int]$state.machineDomState.viewportCanvasCount -lt 1 -or [int]$state.machineDomState.axisCanvasCount -lt 1) {
                $canvasText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport lost the main or world-axis WebGL canvas: $canvasText"
            }
            if (-not $state.machineDomState.axisGizmoLeft -or -not $state.machineDomState.axisGizmoBottom) {
                $axisText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport world axis gizmo is not placed at the left-bottom corner: $axisText"
            }
            if ($state.machineDomState.hasMachineImportPathInput) {
                throw "Machine left pane still contains the removed import path input."
            }
        }

        if ($CheckWorkbenchResizeWorkflow) {
            if (-not $state.workbenchResizeResult) {
                throw "Workbench resize workflow result is missing."
            }
            if ([double]$state.workbenchResizeResult.afterLeft -le [double]$state.workbenchResizeResult.beforeLeft) {
                throw "Left pane resize did not increase width."
            }
            if ([double]$state.workbenchResizeResult.afterRight -le [double]$state.workbenchResizeResult.beforeRight) {
                throw "Right pane resize did not increase width."
            }
        }
    } finally {
        $socket.Dispose()
    }
} finally {
    if ($process -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
    }
}
