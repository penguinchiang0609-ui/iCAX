param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$NativeDirectory = '',
    [switch]$NativeOnly
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$sourceRoot = Join-Path $repo 'src'
if (!$NativeDirectory) { $NativeDirectory = "src/x64/$Configuration" }
$buildRoot = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($NativeDirectory)) { $NativeDirectory } else { Join-Path $repo $NativeDirectory }))
$outputRoot = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $repo $OutputDirectory }))
if (Test-Path -LiteralPath $outputRoot) { throw "Choose a new package directory: $outputRoot" }
$kernelRoot = Join-Path $sourceRoot 'iCAX-Engine/foundation/iPlaneNesting/dist/Release'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$toolRoot = Get-ChildItem -LiteralPath (Join-Path $visualStudio 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $toolRoot.FullName 'bin/Hostx64/x64/dumpbin.exe'
if (!(Test-Path -LiteralPath $dumpbin)) { throw 'Visual Studio dumpbin is required for dependency validation.' }
$crtRoots = @((Join-Path $visualStudio 'VC/Redist/MSVC') | Where-Object { Test-Path -LiteralPath $_ } | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Directory | Sort-Object Name -Descending | ForEach-Object {
        Join-Path $_.FullName 'debug_nonredist/x64/Microsoft.VC143.DebugCRT'
        Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'
    }
})
$manifestPath = Join-Path $sourceRoot 'apps/iplane-cam/product.manifest.json'
$manifestText = [IO.File]::ReadAllText($manifestPath)
$manifest = $manifestText | ConvertFrom-Json
$queue = [Collections.Generic.Queue[string]]::new()
if (!$NativeOnly) {
    foreach ($name in @('iPlaneCAM.exe', 'CefUIContainer.dll', 'libEGL.dll', 'libGLESv2.dll', 'vk_swiftshader.dll', 'vulkan-1.dll')) { $queue.Enqueue($name) }
} else {
    foreach ($name in @('iPlaneCAMNativeBridge.exe', 'iPlaneCAMTest.exe')) { $queue.Enqueue($name) }
}
foreach ($property in $manifest.backend.modules.PSObject.Properties) {
    foreach ($module in $property.Value) { $queue.Enqueue([IO.Path]::GetFileName($module)) }
}
$queue.Enqueue('iPlaneNesting.dll')
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$external = @{}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
while ($queue.Count) {
    $name = $queue.Dequeue()
    if (!$seen.Add($name)) { continue }
    $source = if ($name -eq 'iPlaneNesting.dll') { Join-Path $kernelRoot 'bin/iPlaneNesting.dll' }
        elseif ($external.ContainsKey($name)) { $external[$name] }
        else { Join-Path $buildRoot $name }
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Required runtime missing: $source" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $outputRoot $name)
    $dependencies = & $dumpbin /nologo /dependents $source
    if ($LASTEXITCODE) { throw "Cannot inspect native dependencies: $source" }
    foreach ($line in $dependencies) {
        if ($line -notmatch '^\s+([\w.\-]+\.dll)\s*$') { continue }
        $dependency = $Matches[1]
        if ($dependency -eq 'iPlaneNesting.dll' -or (Test-Path -LiteralPath (Join-Path $buildRoot $dependency))) { $queue.Enqueue($dependency) }
        elseif ($dependency -match '^(?i:msvcp|msvcr|vcruntime|concrt)[\w.]*\.dll$|^(?i:ucrtbased)\.dll$') {
            $candidates = @($crtRoots | ForEach-Object { Join-Path $_ $dependency }) + @(Join-Path "$env:windir/System32" $dependency)
            $candidate = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
            if (!$candidate) { throw "Missing C++ runtime: $dependency" }
            $external[$dependency] = $candidate
            $queue.Enqueue($dependency)
        } elseif ($dependency -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path -LiteralPath (Join-Path "$env:windir/System32" $dependency))) {
            throw "Unresolved native dependency $dependency in $name"
        }
    }
}
function Copy-SourceTree([string]$Relative) {
    $from = Join-Path $sourceRoot $Relative
    $to = Join-Path $outputRoot $Relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $to)) | Out-Null
    Copy-Item -LiteralPath $from -Destination $to -Recurse
}
foreach ($relative in @('apps/iplane-cam/webpage', 'apps/_shared/workbench', 'iCAX-UI/SDK', 'iCAX-UI/AppProxy', 'iCAX-UI/ProductProxy', 'iCAX-UI/ProjectProxy', 'iCAX-UI/SceneProxy', 'iCAX-UI/UI')) { Copy-SourceTree $relative }
$productRoot = Join-Path $outputRoot 'apps/iplane-cam'
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $productRoot 'product.manifest.json'), $manifestText, $utf8)
[IO.File]::WriteAllText((Join-Path $outputRoot 'Application.Product.Setting'), "apps/iplane-cam/product.manifest.json`n", $utf8)
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'apps/branding')) | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot 'apps/branding/icax.ico') -Destination (Join-Path $outputRoot 'apps/branding/icax.ico')
[IO.File]::WriteAllText((Join-Path $outputRoot 'apps/Branding.Setting'), "platformIcon=branding/icax.ico`nproduct.icax.iplane-cam=branding/icax.ico`n", $utf8)
if (!$NativeOnly) {
    foreach ($name in @('chrome_100_percent.pak', 'chrome_200_percent.pak', 'icudtl.dat', 'resources.pak', 'v8_context_snapshot.bin', 'vk_swiftshader_icd.json', 'locales')) {
        Copy-Item -LiteralPath (Join-Path $buildRoot $name) -Destination $outputRoot -Recurse
    }
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot 'Setting')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $outputRoot 'Setting/UIContainer.Setting'), "type=cef`nmodulePath=CefUIContainer.dll`nsdoPollIntervalMS=16`n", $utf8)
    [IO.File]::WriteAllText((Join-Path $outputRoot 'Start-iPlaneCAM.cmd'), "@echo off`r`nset `"ICAX_USER_DATA_ROOT=%~dp0UserData`"`r`ncd /d `"%~dp0`"`r`nstart `"`" `"%~dp0iPlaneCAM.exe`"`r`n", [Text.Encoding]::ASCII)
    $readme = @"
iPlaneCAM $($manifest.productVersion) - $Configuration x64 开发验收版

启动：双击 Start-iPlaneCAM.cmd。请保持整个目录完整。
用户配置和缓存保存在本目录运行后生成的 UserData 中。
工程使用 .ipcam，可通过框架文件菜单保存和重新打开。

底部“命令 / 日志”用选项卡切换，共用可拖动调整高度的框架面板。
切换页签保留命令输入和日志滚动，不中断正在绘制的图形。
捕捉、正交、网格、当前工具和单位位于中央场景下方状态栏，右侧仅保留对象及操作参数。

底图：独立二维工作台，支持 DXF 导入、直线/多段线/矩形/圆/圆弧绘制，
以及移动、复制、旋转、镜像、缩放、删除和吸附。
左侧按“线条 / 曲线 / 形状”分组，五个既有主按钮仍可直接点击。
矩形箭头展开三角形、正多边形、星形；圆箭头展开椭圆；
圆弧箭头展开椭圆弧、贝塞尔、B样条，右键主按钮也可展开。
菜单支持边界翻转和内部滚动；Esc或点击外部关闭，不向画布增加构造点。
七种新增独立图元由原生几何服务输出精确曲段或精确闭合顶点，预览采样不替代正式几何。
椭圆按中心、第一半轴A、另一半轴B构造，可依次Enter或A Tab B Enter配合鼠标方向。
独立椭圆弧另指定终点方向或有符号扫过角θ，非零且绝对值不超过360°，负数为顺时针。
正多边形使用中心、外接圆半径R和首顶点方向；右栏边数3～64，默认6。
星形使用中心、外半径R和内半径r，须0＜r＜R；右栏星角数3～64，默认5。
三角形输入三个顶点；独立贝塞尔依次输入起点、两个控制点、终点，共四点。
独立三次B样条输入4～256个控制点后右键“结束”。无效输入保留已接受的构造点。
以上工具由底图和零件/板材所属草图共用，边数/星角数不占用工程待应用表单草稿。
多段线可连续混合直线、圆弧、椭圆弧、贝塞尔和B样条，保存为一个精确曲线对象。
绘制时右键选择“切换曲段”，在鼠标旁选择五种曲段；当前类型显示勾选。
多段线中的B样条输入足够控制点后，右键“完成本段”即可继续下一段，区别于独立B样条“结束”。
Ribbon和快捷键作为辅助入口，无需记快捷键也能完成连续绘制。
多段线圆弧指定经过点和终点；其椭圆弧依次指定中心、另一半轴半径点和终点方向；
另一半轴点所在侧决定椭圆弧顺/逆时针。其贝塞尔指定两个控制点和终点。
闭合、取消、结束保留在右键菜单中；未完成曲段会显示说明并保护已输入的点。
混合曲线支持整体变换、阵列和闭合，单段尺寸与控制点暂只读。
线条、曲线、形状、修改、变换与阵列、图层、辅助与视图按区组织，更多命令使用共享菜单。
选中零件或板材可进入“编辑草图”，复用同一个视口与二维编辑器。
草图“完成”检查闭合材料后更新原对象；“取消”恢复进入时的草图，不改变原对象。
图层参照三维 CAM 的右侧竖向色层栏，初始提供 8 个色层，可自定义增加。
选中一个或多个刀路后点击色层即可归层；没有选择时点击可设为新建对象的当前层。
色层栏可以折叠，通过管理入口改名、配色、显示/锁定，删除占用层时迁移到接收层。
图层名称、颜色、当前层和对象归属保存于 EC，支持撤销重做及保存重开。
直线/多段线支持已有节点 X/Y 数值编辑；圆/弧通过尺寸编辑。
矩形阵列和圆周阵列在后台计算，正式修改为一次事务，可撤销重做。
绘制可逐点单击，直线、矩形和圆也可按住拖画；右侧提示当前步骤。
圆心确定后，在半径尺寸线上提供 R=；矩形起点确定后，在两条尺寸线上分别提供 W=、H=。
直接键入数字或按 Tab 进入输入。W 按 Enter 确认后输入 H，H 按 Enter 完成；
圆的 R 按 Enter 完成。尺寸输入可配合鼠标方向和实时预览。
圆弧依次输入 R、起始角 α、扫过角 θ，最后按 Enter 创建；也可继续按圆心、起点、终点三点绘制。
起始角以 +X 方向为 0°、逆时针增加；扫过角须大于 0° 且不超过 360°。
选中一个底图图形可查看实际尺寸；双击尺寸文字可原位编辑。
圆支持 R，圆弧支持 R、α 和 θ，直线/两点开放折线支持 L，正交矩形支持局部 W/H。
角度标注带度数和角度弧线；修改起始角保留扫过角，修改扫过角保留起始方向。
编辑矩形时可用 Tab 切换 W/H，Enter 一次提交，Esc 放弃尺寸草稿；移开焦点不提交。
复杂或复合轮廓显示只读外形宽高。尺寸线随缩放更新，正式修改支持撤销、重做及保存重开。
绘制过程中在光标处右键选择“闭合 / 结束 / 取消”，无需移到侧栏。
场景左侧竖向工具栏提供选择、绘图、文字、图片和橡皮擦。
对象列表默认收起，点工具栏“对象面板”展开；下料零件列表仍常驻。
文字工具可点击画布定位，再在右侧输入内容并添加；支持改字、字体和大小。
图片作为参考图导入，文字和图片均支持选择和整体变换。
选中参考图片可配置阈值、反色、小轮廓过滤和简化精度，主动转为加工曲线；原图保留。
下料中可将选中零件或一张结果板加载到底图，形成有来源关系的独立加工副本。
底图中对副本进行二次编辑，原零件和排样方案保持各自的数据身份。
折线至少三个顶点可闭合，结束保留开放折线，取消放弃未提交的输入。
多段线已有一段后，预览同时显示当前段长度 L 和与上一段前进切向的夹角 ∠。
直行 0°、垂直转弯 90°、折返 180°；首段或零长段不显示夹角，标注只读，重叠可缩放查看。
C 或点击首点也可闭合，Enter 开放完成，Esc 取消；支持撤销和重做。
选中已完成的开放折线，可在画布右键选择“闭合轮廓”。
下料：左侧导入零件，底部导入板材或建立矩形板材，右侧设置数量、
间距、板边距、旋转、镜像和时间预算，再运行排样并查看/导出报告。
板材支持不规则外轮廓及孔。底图不用于生成零件或板材。

DXF 当前支持 LINE、LWPOLYLINE（含 bulge）、CIRCLE、ARC。
缺少单位时需明确选择。未支持的实体会拒绝导入。
DXF的ELLIPSE/SPLINE实体导入仍未实现，独立绘制不代表支持对应DXF实体。
修剪/延伸、偏移/圆角、
图层线型、节点增删及手动排样尚未提供。
工艺和 NC 输出为后续独立扩展，本版未实现。

底图、草图/曲线/编辑备份、图层、零件、库存、编辑和排样任务、结果、板和排入件均为原生 ECS 数据；
资源保存源文件、几何和派生显示材质。报告面积及有效性针对计算轮廓（含孔），
曲线离散和连接误差计入计算间距。
二维几何通过注入的 PlaneGeometryServices.dll 调用；排样通过 PlaneNestingServices.dll
调用独立 iPlaneNesting.dll，SDO 负责业务调度、事务、版本与结果发布。

本轮r13验收证据目录：output/tests/iplane-cam-shapes。
最终通过数与交付结论以本轮完整核验汇总为准，历史r12统计不计作r13通过证据。
尚未完成独立桌面 CEF 窗口实操及完整 Release 产品验收。
第三方声明见 Licenses 及 runtime/cad/libredwg。
"@
    [IO.File]::WriteAllText((Join-Path $outputRoot 'README.txt'), $readme, $utf8)
}
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'Licenses')) | Out-Null
Copy-Item -LiteralPath (Join-Path $kernelRoot 'licenses') -Destination (Join-Path $outputRoot 'Licenses/iPlaneNesting') -Recurse
$occtNotices = Join-Path $sourceRoot 'third_party/opencascade/occt-8.0.0-p1'
[IO.Directory]::CreateDirectory((Join-Path $outputRoot 'Licenses/OpenCascade')) | Out-Null
foreach ($notice in @('LICENSE_LGPL_21.txt', 'OCCT_LGPL_EXCEPTION.txt')) {
    Copy-Item -LiteralPath (Join-Path $occtNotices $notice) -Destination (Join-Path $outputRoot "Licenses/OpenCascade/$notice")
}
# The shared CAD reader's external converter is a runtime dependency for binary
# CAD exchange. Preserve its notices/source offer together with that runtime.
$cadRuntime = Join-Path $buildRoot 'runtime/cad/libredwg'
if (Test-Path -LiteralPath $cadRuntime) {
    [IO.Directory]::CreateDirectory((Join-Path $outputRoot 'runtime/cad')) | Out-Null
    Copy-Item -LiteralPath $cadRuntime -Destination (Join-Path $outputRoot 'runtime/cad/libredwg') -Recurse
}
foreach ($property in $manifest.backend.modules.PSObject.Properties) {
    foreach ($module in $property.Value) {
        $resolved = $module.Replace('${ExecutableDirectory}', $outputRoot)
        if ($resolved.Contains('${') -or !(Test-Path -LiteralPath $resolved -PathType Leaf)) { throw "Packaged module missing: $module" }
    }
}
$forbidden = @(Get-ChildItem -LiteralPath $outputRoot -Recurse -File | Where-Object {
    $_.FullName -match '[\\/](UserData|Cache|Logs|node_modules|__pycache__)[\\/]' -or $_.Extension -in @('.tdlic', '.pem', '.key', '.pfx', '.p12', '.log', '.ipcam', '.pdb', '.pyc')
})
if ($forbidden.Count) { throw "Unexpected local data in package: $($forbidden.FullName -join ', ')" }
if (!$NativeOnly) {
    $testPrograms = @(Get-ChildItem -LiteralPath $outputRoot -Recurse -File | Where-Object { $_.Name -match '(?i)(Test|NativeBridge)\.exe$' })
    if ($testPrograms.Count) { throw "Unexpected test program in product package: $($testPrograms.FullName -join ', ')" }
}
$inventory = @(Get-ChildItem -LiteralPath $outputRoot -Recurse -File | ForEach-Object {
    [pscustomobject]@{ path = $_.FullName.Substring($outputRoot.Length + 1).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[IO.File]::WriteAllText((Join-Path $outputRoot 'package-files.json'), (ConvertTo-Json -InputObject $inventory -Depth 4), $utf8)
Write-Output "Packaged $($inventory.Count) files at $outputRoot"
