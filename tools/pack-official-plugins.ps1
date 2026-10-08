param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$ReleaseTag,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\plugin-release'),
    [string]$NativeArtifactDirectory = (Join-Path $PSScriptRoot '..\dist\Release'),
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$DevelopmentPluginId
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\official-plugin-packages'))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$entries = @()

foreach ($idDirectory in Get-ChildItem -LiteralPath $sourceRoot -Directory) {
    if ($DevelopmentPluginId -and $idDirectory.Name -cne $DevelopmentPluginId) {
        continue
    }
    foreach ($versionDirectory in Get-ChildItem -LiteralPath $idDirectory.FullName -Directory) {
        $releaseStatusPath = Join-Path $versionDirectory.FullName 'release-status.json'
        if (Test-Path -LiteralPath $releaseStatusPath -PathType Leaf) {
            $releaseStatus = Get-Content -LiteralPath $releaseStatusPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($releaseStatus.publish -eq $false -and -not $DevelopmentPluginId) {
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
        if ($DevelopmentPluginId) {
            # 本地测试走已有第三方包校验，不能冒充已验证的 Native Release 来源。
            $manifest.id = 'dev.' + $manifest.id
        }
        $assetName = "$($manifest.id)-$($manifest.version).ssmtpkg"
        $assetPath = Join-Path $outputRoot $assetName
        if (Test-Path -LiteralPath $assetPath) {
            throw "目标资产已存在，请使用空输出目录: $assetPath"
        }
        # 编译产物仅进入临时 staging，不能手工放入受版本控制的包来源目录。
        $stagingRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-plugin-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::CreateDirectory($stagingRoot) | Out-Null
        try {
            # 发布状态属于构建侧元信息，不写进可安装插件包。
            Get-ChildItem -LiteralPath $versionDirectory.FullName -Force |
                Where-Object { $_.Name -cne 'release-status.json' } |
                Copy-Item -Destination $stagingRoot -Recurse
            if ($DevelopmentPluginId) {
                [IO.File]::WriteAllText(
                    (Join-Path $stagingRoot 'ssmt-plugin.json'),
                    ($manifest | ConvertTo-Json -Depth 20),
                    [Text.UTF8Encoding]::new($false)
                )
            }
            foreach ($runtime in $manifest.contributions.runtimePlugins) {
                $runtimePath = [string]$runtime.path
                if ([string]::IsNullOrWhiteSpace($runtimePath) -or [IO.Path]::IsPathRooted($runtimePath) -or
                    $runtimePath -match '(^|[\\/])\.\.([\\/]|$)' -or $runtimePath -match ':') {
                    throw "非法 Runtime 插件路径: $runtimePath"
                }
                $destination = [IO.Path]::GetFullPath((Join-Path $stagingRoot $runtimePath))
                if (-not $destination.StartsWith($stagingRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                    throw "Runtime 插件路径超出包目录: $runtimePath"
                }
                if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
                    $artifact = Join-Path $NativeArtifactDirectory ([IO.Path]::GetFileName($destination))
                    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) {
                        throw "未构建 Runtime 插件: $artifact"
                    }
                    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
                    Copy-Item -LiteralPath $artifact -Destination $destination
                }
            }
            [IO.Compression.ZipFile]::CreateFromDirectory(
                $stagingRoot, $assetPath, [IO.Compression.CompressionLevel]::Optimal, $false
            )
        }
        finally {
            # stagingRoot 是此处直接生成的临时绝对目录，清理前再次核对其范围。
            $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
            $resolvedStaging = [IO.Path]::GetFullPath($stagingRoot)
            if ($resolvedStaging.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
                [IO.Path]::GetFileName($resolvedStaging) -match '^ssmt-plugin-[0-9a-f]{32}$') {
                Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
            }
        }
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

if ($DevelopmentPluginId) {
    if ($entries.Count -eq 0) { throw "未找到可打包的开发插件: $DevelopmentPluginId" }
    Write-Output $outputRoot
    return
}

$catalogPath = Join-Path $outputRoot 'ssmt-plugin-catalog.json'
if (Test-Path -LiteralPath $catalogPath) {
    throw "目标目录已存在插件目录，请使用空输出目录: $catalogPath"
}
$catalog = [ordered]@{ schemaVersion = 1; entries = @($entries) }
$json = $catalog | ConvertTo-Json -Depth 20
[IO.File]::WriteAllText($catalogPath, $json, [Text.UTF8Encoding]::new($false))
Write-Output $outputRoot
