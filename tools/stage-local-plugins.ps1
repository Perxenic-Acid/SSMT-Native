[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$NativeArtifactDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string[]]$ExcludePluginId = @()
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../official-plugin-packages'))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$artifactRoot = [IO.Path]::GetFullPath($NativeArtifactDirectory)
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-local-plugins-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($temporaryRoot) | Out-Null
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
try {
    foreach ($plugin in Get-ChildItem -LiteralPath $sourceRoot -Directory) {
        if ($ExcludePluginId -contains $plugin.Name) { continue }
        foreach ($version in Get-ChildItem -LiteralPath $plugin.FullName -Directory) {
            $manifest = Get-Content -LiteralPath (Join-Path $version.FullName 'ssmt-plugin.json') -Raw -Encoding UTF8 | ConvertFrom-Json
            # 未构建的可选目标不生成不完整插件包；纯本体 / 外部工具插件无需 DLL。
            $missing = @($manifest.contributions.runtimePlugins | Where-Object {
                $null -ne $_ -and -not (Test-Path -LiteralPath (Join-Path $artifactRoot ([IO.Path]::GetFileName($_.path))) -PathType Leaf)
            })
            $buildArtifactsPath = Join-Path $version.FullName 'build-artifacts.json'
            if (Test-Path -LiteralPath $buildArtifactsPath -PathType Leaf) {
                $missing += @((Get-Content -LiteralPath $buildArtifactsPath -Raw -Encoding UTF8 | ConvertFrom-Json).files | Where-Object {
                    -not (Test-Path -LiteralPath (Join-Path $artifactRoot $_.source) -PathType Leaf)
                })
            }
            # 新版本增加辅助组件时，只跳过该版本，已构建的其他版本仍可用于本地验证。
            if ($missing.Count) { Write-Warning "跳过尚未构建的插件：$($plugin.Name) $($version.Name)"; continue }
            & (Join-Path $PSScriptRoot 'pack-official-plugins.ps1') -ReleaseTag local-build `
                -DevelopmentPluginId $plugin.Name -PluginVersion $version.Name -LocalBuild `
                -NativeArtifactDirectory $artifactRoot -OutputDirectory $temporaryRoot | Out-Null
        }
    }
    foreach ($package in Get-ChildItem -LiteralPath $temporaryRoot -Filter '*.ssmtpkg' -File) {
        Copy-Item -LiteralPath $package.FullName -Destination (Join-Path $outputRoot $package.Name) -Force
        Write-Host "本地插件包：$($package.Name)"
    }
} finally {
    # 只删除本次创建、位于系统临时目录的 staging。
    $resolvedTemporary = [IO.Path]::GetFullPath($temporaryRoot)
    $tempParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($resolvedTemporary.StartsWith($tempParent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTemporary) -match '^ssmt-local-plugins-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolvedTemporary -Recurse -Force
    }
}
