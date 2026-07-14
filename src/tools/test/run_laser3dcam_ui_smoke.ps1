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
    [switch]$CheckMachineEnableWorkflow,
    [switch]$CheckMachineSelectionWorkflow,
    [switch]$CheckWorkbenchResizeWorkflow,
    [switch]$RequireRenderable,
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

function Resolve-DefaultApplicationPath {
    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $central = Join-Path $root "iCAX-Engine\x64\Debug\Application.exe"
    if (Test-Path -LiteralPath $central) {
        return $central
    }
    return Join-Path $root "iCAX-Application\Application\x64\Debug\Application.exe"
}

function New-DefaultProjectPath {
    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $dir = Join-Path $root ".codex_tmp\ui-smoke"
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
    return Join-Path $dir "laser3dcam-ui-smoke-$stamp.i3cam"
}

function Sync-ApplicationRuntimeDependencies {
    param([string]$ApplicationPath)

    $root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $appDir = Split-Path $ApplicationPath
    $engineRuntime = Join-Path $root "iCAX-Engine\x64\Debug"
    $runtimeProjects = @(
        "iCAX-Engine\foundation\Data",
        "iCAX-Engine\foundation\GeometryData",
        "iCAX-Engine\framework\ApplicationContext",
        "iCAX-Engine\framework\ApplicationHost",
        "iCAX-Engine\framework\Behaviour",
        "iCAX-Engine\framework\CommandTargets",
        "iCAX-Engine\framework\Database",
        "iCAX-Engine\framework\Mailbox",
        "iCAX-Engine\framework\MailHandler",
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
        "iCAX-Plugins\geometry\DAEResourceImport",
        "iCAX-Plugins\geometry\STLResourceImport",
        "iCAX-Plugins\input\InputPDO",
        "iCAX-Plugins\input\InputService",
        "iCAX-Plugins\physics\ColliderData",
        "iCAX-Plugins\render\CameraNavigation",
        "iCAX-Plugins\render\PDORenderService",
        "iCAX-Plugins\render\RenderData",
        "iCAX-Plugins\render\RenderInteraction",
        "iCAX-Plugins\render\RenderPDO",
        "iCAX-Plugins\render\RenderService"
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
            if (Test-Path -LiteralPath $projectDll) {
                Get-Item -LiteralPath $projectDll
                continue
            }
            if (Test-Path -LiteralPath $sharedDll) {
                Get-Item -LiteralPath $sharedDll
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
            Add-ModulePath -Paths $modulePaths -Value $manifest.backend.modules.commands
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
        "mailPollIntervalMS=16"
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
Ensure-UIContainerSmokeConfig -WorkingDirectory $WorkingDirectory -RemoteDebuggingPort $RemoteDebuggingPort
if (-not $ProjectPath) {
    $ProjectPath = New-DefaultProjectPath
}

$process = $null
if ($StartApplication) {
    $process = Start-Process -FilePath $ApplicationPath -WorkingDirectory $WorkingDirectory -PassThru
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
        $createProjectLiteral = if ($CreateProject -or $MachineDefinitionPath) { "true" } else { "false" }
        $importMachineLiteral = if ($MachineDefinitionPath) { "true" } else { "false" }
        $checkMachineEnableWorkflowLiteral = if ($CheckMachineEnableWorkflow) { "true" } else { "false" }
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
    await waitUntil(() => window.__icaxLaser3DCAM, "Laser3DCAM automation");
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

  const viewport = window.__icaxLaser3DCAM?.getViewportDebugState?.({ samplePixels: true, includeObjects: $checkMachineSelectionWorkflowLiteral }) ?? null;
  return {
    href: location.href,
    title: document.title,
    app: window.__icaxAppShell?.getState?.() ?? null,
    productDiagnosticsReady: Boolean(window.__icaxLaser3DCAM),
    product: window.__icaxLaser3DCAM?.getState?.() ?? null,
    createProjectResult,
    importMachineResult,
    machineEnableResult,
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
            if ([int]$state.viewport.cameraCount -le 0 -or -not $state.viewport.activeCameraId) {
                throw "Viewport has no active camera."
            }
            if ($state.viewport.pixelSample -and [int]$state.viewport.pixelSample.nonBackground -le 0) {
                throw "Viewport rendered only background pixels."
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
            if (-not $state.machineDomState -or [int]$state.machineDomState.viewCubeButtonCount -lt 20) {
                throw "Machine viewport view cube is missing."
            }
            if (-not $state.machineDomState.viewCubePitch -or -not $state.machineDomState.viewCubeYaw) {
                $viewCubeText = $state.machineDomState | ConvertTo-Json -Depth 8 -Compress
                throw "Machine viewport view cube did not synchronize camera orientation: $viewCubeText"
            }
            if (-not $state.machineDomState.hasAxisGizmo) {
                throw "Machine viewport axis gizmo is missing."
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
