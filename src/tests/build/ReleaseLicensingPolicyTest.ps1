$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$validator = Join-Path $repository 'src/tools/build/validate_tube_designer_licensing.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($validator, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors.Message -join '; ') }
$function = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-LicenseBuildPolicy' }, $true)
. ([scriptblock]::Create($function.Extent.Text))
$catalog = Get-Content -LiteralPath (Join-Path $repository 'src/licensing/features.json') -Raw | ConvertFrom-Json
[long]$mask = 0
foreach ($feature in $catalog.features) { $mask = $mask -bor [long]$feature.bit }
$valid = @{product='icax.tube-designer';developmentBypass=$false;authorizationEnforced=$true;configured=$true;issuer='policy-test';certificateFormat=$catalog.certificateFormat;featureSchemaVersion=$catalog.schemaVersion;knownFeatures=$mask;featureCatalog=$catalog.features}
Assert-LicenseBuildPolicy ([pscustomobject]$valid) $catalog
$checks = @(
    @{key='developmentBypass';value=$true},
    @{key='authorizationEnforced';value=$false},
    @{key='configured';value=$false},
    @{key='issuer';value='unconfigured'},
    @{key='product';value='other.product'},
    @{key='certificateFormat';value='TDLIC001'},
    @{key='featureSchemaVersion';value=999},
    @{key='knownFeatures';value=15}
    @{key='featureCatalog';value=@($catalog.features | Select-Object -Skip 1)}
)
foreach ($case in $checks) {
    $policy = $valid.Clone()
    $policy[$case.key] = $case.value
    $rejected = $false
    try { Assert-LicenseBuildPolicy ([pscustomobject]$policy) $catalog } catch { $rejected = $true }
    if (-not $rejected) { throw "Release policy incorrectly accepted $($case.key)" }
}
foreach ($field in @('id','bit','label','parent')) {
    $features = $catalog.features | ConvertTo-Json -Depth 5 | ConvertFrom-Json
    switch ($field) {
        'id' { $features[4].id = 'product.other' }
        'bit' { $features[4].bit = 32; $features[5].bit = 16 }
        'label' { $features[4].label = 'Different label' }
        'parent' { $features[4].parent = 'page.nesting' }
    }
    $policy = $valid.Clone()
    $policy.featureCatalog = $features
    $rejected = $false
    try { Assert-LicenseBuildPolicy ([pscustomobject]$policy) $catalog } catch { $rejected = $true }
    if (-not $rejected) { throw "Release policy incorrectly accepted changed feature $field" }
}
Write-Output 'Release licensing policy regression passed: 1 configured current contract and 13 rejected invalid policies.'
