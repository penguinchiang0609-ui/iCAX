param(
    [int]$Port = 4173
)

$env:ICAX_WEBPAGE_PORT = "$Port"
node "$PSScriptRoot\dev-server.mjs"
