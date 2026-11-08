#Requires -Version 7.0
[CmdletBinding()]
param(
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\dist\Release'),
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build-plugin-backends.ps1') -Backends @('player-tweaks') -ArtifactDirectory $ArtifactDirectory -Release:$Release
