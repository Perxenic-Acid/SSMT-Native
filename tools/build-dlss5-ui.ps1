#Requires -Version 7.0
[CmdletBinding()]
param([string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\dist\Release'))
$ErrorActionPreference = 'Stop'
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$uiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\dlss5-integration-ui'))
$dependencies = Join-Path $uiRoot 'node_modules'
if (-not (Test-Path -LiteralPath (Join-Path $dependencies 'vite\bin\vite.js'))) {
    $dependencies = Join-Path $workspace 'node_modules'
}
$output = [IO.Path]::GetFullPath($ArtifactDirectory)
& bun (Join-Path $dependencies 'vue-tsc\bin\vue-tsc.js') --noEmit --project (Join-Path $uiRoot 'tsconfig.json')
if ($LASTEXITCODE -ne 0) { throw 'DLSS5 插件页面类型检查失败' }
& bun (Join-Path $dependencies 'vite\bin\vite.js') build --config (Join-Path $uiRoot 'vite.config.ts') --outDir $output
if ($LASTEXITCODE -ne 0) { throw 'DLSS5 插件页面构建失败' }
