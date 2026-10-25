$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$nativeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-package-test-' + [Guid]::NewGuid().ToString('N'))
function Assert-True([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
    $tools = Join-Path $testRoot 'tools'
    $packages = Join-Path $testRoot 'official-plugin-packages/ssmt.dlss5.integration'
    $artifacts = Join-Path $testRoot 'artifacts'
    New-Item -ItemType Directory -Path $tools, $packages, $artifacts | Out-Null
    Copy-Item -LiteralPath (Join-Path $nativeRoot 'tools/pack-official-plugins.ps1'),
        (Join-Path $nativeRoot 'tools/stage-local-plugins.ps1'),
        (Join-Path $nativeRoot 'tools/validate-plugin-manifest.ps1') -Destination $tools
    foreach ($version in @('0.2.2', '0.2.3')) {
        Copy-Item -LiteralPath (Join-Path $nativeRoot "official-plugin-packages/ssmt.dlss5.integration/$version") -Destination $packages -Recurse
    }
    Copy-Item -LiteralPath (Join-Path $nativeRoot 'official-plugin-packages/ssmt.poser.genshin-probe') -Destination (Split-Path $packages) -Recurse
    $injectedPackage = Join-Path (Split-Path $packages) 'ssmt.injected.fixture/1.0.0'
    New-Item -ItemType Directory -Path $injectedPackage, (Join-Path $injectedPackage 'plugin') | Out-Null
    [IO.File]::WriteAllText((Join-Path $injectedPackage 'ssmt-plugin.json'), (@{
        schemaVersion = 1
        id = 'ssmt.injected.fixture'
        name = 'Injected fixture'
        version = '1.0.0'
        author = 'SSMT tests'
        compatibility = @{ ssmt = '>=4.2.0'; platforms = @('windows-x64'); games = @('EFMI') }
        contributions = @{ injectedDlls = @(@{ path = 'plugin/fixture.dll'; sha256 = ('0' * 64) }) }
        externalDependencies = @()
        permissions = @('native.inject')
    } | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $injectedPackage 'release-status.json'),
        '{"publish":true}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $injectedPackage 'build-artifacts.json'),
        '{"files":[{"source":"fixture.dll","destination":"plugin/fixture.dll"}]}', [Text.UTF8Encoding]::new($false))
    $invalidTestRoot = Join-Path $testRoot 'invalid-manifest'
    $invalidTools = Join-Path $invalidTestRoot 'tools'
    $invalidPackage = Join-Path $invalidTestRoot 'official-plugin-packages/ssmt.invalid.manifest/1.0.0'
    New-Item -ItemType Directory -Path $invalidTools, $invalidPackage | Out-Null
    Copy-Item -LiteralPath (Join-Path $nativeRoot 'tools/pack-official-plugins.ps1'),
        (Join-Path $nativeRoot 'tools/validate-plugin-manifest.ps1') -Destination $invalidTools
    [IO.File]::WriteAllText((Join-Path $invalidPackage 'ssmt-plugin.json'), (@{
        schemaVersion = 1
        id = 'ssmt.invalid.manifest'
        name = 'Invalid manifest'
        version = '1.0.0'
        author = 'SSMT tests'
        compatibility = @{ ssmt = '>=4.2.0'; platforms = @('windows-x64'); games = @('EFMI') }
        contributions = @{
            injectedDlls = @(@{
                path = 'plugin/fixture.dll'
                description = 'Nested fields are also strict'
            })
        }
        externalDependencies = @()
        permissions = @()
    } | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $invalidPackage 'release-status.json'),
        '{"publish":true}', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $artifacts 'SSMT-Poser.dll'), 'poser fixture')
    [IO.File]::WriteAllText((Join-Path $artifacts 'SSMT-Gimi-Bridge.dll'), 'bridge fixture')
    [IO.File]::WriteAllText((Join-Path $artifacts 'SSMT-Gimi-Bridge-LICENSE.txt'), 'bridge license fixture')
    [IO.File]::WriteAllText((Join-Path $artifacts 'fixture.dll'), 'injected dll fixture')
    $stage = Join-Path $tools 'stage-local-plugins.ps1'
    $pack = Join-Path $tools 'pack-official-plugins.ps1'
    $invalidPack = Join-Path $invalidTools 'pack-official-plugins.ps1'
    $invalidFailed = $false
    try { & $invalidPack -ReleaseTag v-test -OutputDirectory (Join-Path $testRoot 'invalid-manifest-release') -NativeArtifactDirectory $artifacts | Out-Null }
    catch { $invalidFailed = $_.Exception.Message -like '*unsupported field*description*' }
    Assert-True $invalidFailed '正式打包必须拒绝 SSMT 不支持的嵌套清单字段'
    $oldOnly = Join-Path $testRoot 'old-only'
    & $stage -NativeArtifactDirectory $artifacts -OutputDirectory $oldOnly
    Assert-True (Test-Path "$oldOnly/ssmt.dlss5.integration-0.2.2.ssmtpkg") '缺少新辅助组件不应屏蔽已构建的旧版本'
    Assert-True (-not (Test-Path "$oldOnly/ssmt.dlss5.integration-0.2.3.ssmtpkg")) '不得生成缺少 OptiScaler 的新版插件'
    $failed = $false
    try { & $pack -ReleaseTag v-test -OutputDirectory (Join-Path $testRoot 'incomplete-release') -NativeArtifactDirectory $artifacts | Out-Null }
    catch { $failed = $_.Exception.Message -like '*未构建辅助组件*' }
    Assert-True $failed '正式打包必须拒绝缺少辅助组件的包'

    foreach ($name in @('SSMT-OptiScaler.dll', 'SSMT-OptiScaler-LICENSE.txt', 'SSMT-OptiScaler-source.json', 'SSMT-OptiScaler-THIRD_PARTY_NOTICES.txt')) {
        [IO.File]::WriteAllText((Join-Path $artifacts $name), "fixture $name")
    }
    $complete = Join-Path $testRoot 'complete'
    & $stage -NativeArtifactDirectory $artifacts -OutputDirectory $complete
    $zip = [IO.Compression.ZipFile]::OpenRead((Join-Path $complete 'ssmt.dlss5.integration-0.2.3.ssmtpkg'))
    try {
        foreach ($name in @('optiscaler/OptiScaler.dll', 'optiscaler/LICENSE.txt', 'optiscaler/source.json', 'optiscaler/THIRD_PARTY_NOTICES.txt')) {
            $entry = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -ceq $name })
            $sidecar = @($zip.Entries | Where-Object { $_.FullName.Replace('\', '/') -ceq ($name + '.sha256') })
            Assert-True ($entry.Count -eq 1 -and $sidecar.Count -eq 1) "辅助组件或哈希缺失：$name"
            $stream = $entry[0].Open()
            $hash = [Security.Cryptography.SHA256]::Create()
            try { $expected = ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
            finally { $stream.Dispose(); $hash.Dispose() }
            $reader = [IO.StreamReader]::new($sidecar[0].Open())
            try { Assert-True ($reader.ReadToEnd().Trim() -ceq $expected) "组件旁置哈希不匹配：$name" }
            finally { $reader.Dispose() }
        }
        Assert-True (@($zip.Entries | Where-Object { $_.Name -in @('build-artifacts.json', 'release-status.json') }).Count -eq 0) '构建侧清单不得进入插件包'
    } finally { $zip.Dispose() }
    $release = Join-Path $testRoot 'release'
    & $pack -ReleaseTag v-test -OutputDirectory $release -NativeArtifactDirectory $artifacts | Out-Null
    $catalog = Get-Content "$release/ssmt-plugin-catalog.json" -Raw | ConvertFrom-Json
    Assert-True (@($catalog.entries).Count -eq 4) '正式目录应保留两个 DLSS5 版本、当前 Poser 和注入 DLL 样例'
    $poser = @($catalog.entries | Where-Object id -EQ 'ssmt.poser.genshin-probe')
    Assert-True ($poser.Count -eq 1 -and $poser[0].version -ceq '0.1.1') 'Poser 新版应可发现，旧版不得重打当前 DLL'
    Assert-True (-not (Test-Path "$release/ssmt.poser.genshin-probe-0.1.0.ssmtpkg")) '新 Release 不得复用已发布 Poser 版本'
    $poserZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $release 'ssmt.poser.genshin-probe-0.1.1.ssmtpkg'))
    try {
        Assert-True (@($poserZip.Entries | Where-Object { $_.FullName.Replace('\', '/') -ceq 'native/SSMT-Poser.dll' }).Count -eq 1) '新版 Poser 必须包含 DLL'
        Assert-True (@($poserZip.Entries | Where-Object Name -EQ 'MinHook-LICENSE.txt').Count -eq 1) '新版 Poser 必须保留依赖许可证'
        Assert-True (@($poserZip.Entries | Where-Object Name -EQ 'Bullet-LICENSE.txt').Count -eq 1) '新版 Poser 必须包含静态链接 Bullet 的许可证'
    } finally { $poserZip.Dispose() }
    $injectedZip = [IO.Compression.ZipFile]::OpenRead((Join-Path $release 'ssmt.injected.fixture-1.0.0.ssmtpkg'))
    try {
        $manifestEntry = @($injectedZip.Entries | Where-Object { $_.FullName.Replace('\', '/') -ceq 'ssmt-plugin.json' })
        Assert-True ($manifestEntry.Count -eq 1) '注入 DLL 样例缺少插件清单'
        $reader = [IO.StreamReader]::new($manifestEntry[0].Open())
        try { $packedManifest = $reader.ReadToEnd() | ConvertFrom-Json }
        finally { $reader.Dispose() }
        $expectedHash = (Get-FileHash -LiteralPath (Join-Path $artifacts 'fixture.dll') -Algorithm SHA256).Hash
        Assert-True ($packedManifest.contributions.injectedDlls[0].sha256 -ceq $expectedHash) '打包时未回写实际注入 DLL 哈希'
    } finally { $injectedZip.Dispose() }
    foreach ($entry in $catalog.entries) {
        $packagePath = Join-Path $release ([IO.Path]::GetFileName(([Uri]$entry.downloadUrl).AbsolutePath))
        Assert-True ((Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant() -ceq $entry.sha256) '目录哈希与发布包不一致'
        Assert-True ((Get-Item -LiteralPath $packagePath).Length -eq $entry.packageSize) '目录大小与发布包不一致'
    }
    Write-Host '插件打包回归通过：版本隔离、完整性拒绝、组件哈希与正式目录。'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($resolved.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved) -match '^ssmt-package-test-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
