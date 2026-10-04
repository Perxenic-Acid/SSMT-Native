param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$ReleaseTag,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\plugin-release')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\official-plugin-packages'))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$entries = @()

foreach ($idDirectory in Get-ChildItem -LiteralPath $sourceRoot -Directory) {
    foreach ($versionDirectory in Get-ChildItem -LiteralPath $idDirectory.FullName -Directory) {
        $releaseStatusPath = Join-Path $versionDirectory.FullName 'release-status.json'
        if (Test-Path -LiteralPath $releaseStatusPath -PathType Leaf) {
            $releaseStatus = Get-Content -LiteralPath $releaseStatusPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($releaseStatus.publish -eq $false) {
                continue
            }
        }
        $manifestPath = Join-Path $versionDirectory.FullName 'ssmt-plugin.json'
        if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
            throw "缺少插件清单: $manifestPath"
        }
        $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($manifest.id -cne $idDirectory.Name -or $manifest.version -cne $versionDirectory.Name) {
            throw "插件清单与目录不一致: $manifestPath"
        }
        $assetName = "$($manifest.id)-$($manifest.version).ssmtpkg"
        $assetPath = Join-Path $outputRoot $assetName
        if (Test-Path -LiteralPath $assetPath) {
            throw "目标资产已存在，请使用空输出目录: $assetPath"
        }
        [IO.Compression.ZipFile]::CreateFromDirectory(
            $versionDirectory.FullName,
            $assetPath,
            [IO.Compression.CompressionLevel]::Optimal,
            $false
        )
        $asset = Get-Item -LiteralPath $assetPath
        $sha256 = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
        [string[]]$games = @()
        if ($null -ne $manifest.compatibility.games) {
            $games = [string[]]$manifest.compatibility.games
        }
        $entries += [ordered]@{
            id = $manifest.id
            name = $manifest.name
            description = if ($manifest.description) { $manifest.description } else { $manifest.name }
            author = $manifest.author
            version = $manifest.version
            downloadUrl = "https://github.com/Perxenic-Acid/SSMT-Native/releases/download/$ReleaseTag/$assetName"
            sha256 = $sha256
            minimumSsmtVersion = $manifest.compatibility.ssmt
            supportedPlatforms = @($manifest.compatibility.platforms)
            supportedGames = $games
            packageSize = $asset.Length
            permissions = @($manifest.permissions)
            externalDependencies = @($manifest.externalDependencies)
            bundledThirdPartyPayloads = @()
        }
    }
}

$catalogPath = Join-Path $outputRoot 'ssmt-plugin-catalog.json'
if (Test-Path -LiteralPath $catalogPath) {
    throw "目标目录已存在插件目录，请使用空输出目录: $catalogPath"
}
$catalog = [ordered]@{ schemaVersion = 1; entries = @($entries) }
$json = $catalog | ConvertTo-Json -Depth 20
[IO.File]::WriteAllText($catalogPath, $json, [Text.UTF8Encoding]::new($false))
Write-Output $outputRoot
