param(
    [string]$ApplicationPath = "",
    [string]$WorkingDirectory = "",
    [int]$RemoteDebuggingPort = 9223,
    [switch]$StartApplication,
    [switch]$CreateProject,
    [string]$ProductId = "icax.laser-3d-cam",
    [string]$ProjectName = "Laser3DCAM UI Smoke",
    [string]$ProjectPath = "",
    [string]$MachineDefinitionPath = "",
    [string]$WorkpiecePath = "",
    [switch]$RecognizeCADIntent,
    [switch]$CheckTubeCSGWorkflow,
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
    $prefix = if ($isTubeOne) { "tubeone-ui-smoke" } else { "laser3dcam-ui-smoke" }
    $extension = if ($isTubeOne) { ".tubeone" } else { ".i3cam" }
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
        $createProjectLiteral = if ($CreateProject -or $MachineDefinitionPath -or $WorkpiecePath) { "true" } else { "false" }
        $importMachineLiteral = if ($MachineDefinitionPath) { "true" } else { "false" }
        $importWorkpieceLiteral = if ($WorkpiecePath) { "true" } else { "false" }
        $recognizeCADIntentLiteral = if ($RecognizeCADIntent) { "true" } else { "false" }
        $checkTubeCSGWorkflowLiteral = if ($CheckTubeCSGWorkflow) { "true" } else { "false" }
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
  if ($createProjectLiteral) {
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
