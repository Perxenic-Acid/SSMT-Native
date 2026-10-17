param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$ReleaseTag,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '..\dist\plugin-release'),
    [string]$NativeArtifactDirectory = (Join-Path $PSScriptRoot '..\dist\Release'),
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$DevelopmentPluginId,
    [switch]$LocalBuild
)

$ErrorActionPreference = 'Stop'
if ($LocalBuild -and -not $DevelopmentPluginId) { throw '本地构建必须明确指定插件 ID' }
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
        # 正式目录严格采用显式白名单；没有状态文件的旧插件同样不得发布。
        # 显式开发打包由 -DevelopmentPluginId 选择，不改变正式发布资格。
        if (-not $DevelopmentPluginId) {
            if (-not (Test-Path -LiteralPath $releaseStatusPath -PathType Leaf)) {
                continue
            }
            $releaseStatus = Get-Content -LiteralPath $releaseStatusPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($releaseStatus.publish -isnot [bool] -or -not $releaseStatus.publish) {
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
        if ($DevelopmentPluginId -and -not $LocalBuild) {
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
                Where-Object { $_.Name -notin @('release-status.json', 'build-artifacts.json') } |
                Copy-Item -Destination $stagingRoot -Recurse
            # 插件的辅助组件不属于 Plugin ABI DLL，不能放进 runtimePlugins 自动注入。
            $buildArtifactsPath = Join-Path $versionDirectory.FullName 'build-artifacts.json'
            if (Test-Path -LiteralPath $buildArtifactsPath -PathType Leaf) {
                $buildArtifacts = Get-Content -LiteralPath $buildArtifactsPath -Raw -Encoding UTF8 | ConvertFrom-Json
                foreach ($item in $buildArtifacts.files) {
                    $sourceName = [string]$item.source
                    $relative = [string]$item.destination
                    if ($sourceName -notmatch '^[A-Za-z0-9._-]+$' -or [string]::IsNullOrWhiteSpace($relative) -or
                        [IO.Path]::IsPathRooted($relative) -or $relative -match '(^|[\\/])\.\.([\\/]|$)' -or $relative -match ':') {
                        throw '非法辅助构建产物路径'
                    }
                    $artifact = Join-Path $NativeArtifactDirectory $sourceName
                    $destination = [IO.Path]::GetFullPath((Join-Path $stagingRoot $relative))
                    if (-not $destination.StartsWith($stagingRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                        throw "辅助构建产物越出包目录: $relative"
                    }
                    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "未构建辅助组件: $artifact" }
                    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
                    Copy-Item -LiteralPath $artifact -Destination $destination
                    # 哈希随已验证的完整插件包分发；启动时再次核对组件，拒绝损坏文件。
                    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
                    [IO.File]::WriteAllText($destination + '.sha256', $hash, [Text.UTF8Encoding]::new($false))
                }
            }
            if ($DevelopmentPluginId -and -not $LocalBuild) {
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
            # 缺省可选数组必须输出 []，@($null) 会生成 [null] 并使客户端下载整个目录失败。
            permissions = @($manifest.permissions | Where-Object { $null -ne $_ })
            externalDependencies = @($manifest.externalDependencies | Where-Object { $null -ne $_ })
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
