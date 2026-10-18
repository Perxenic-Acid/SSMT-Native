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
        (Join-Path $nativeRoot 'tools/stage-local-plugins.ps1') -Destination $tools
    foreach ($version in @('0.2.2', '0.2.3')) {
        Copy-Item -LiteralPath (Join-Path $nativeRoot "official-plugin-packages/ssmt.dlss5.integration/$version") -Destination $packages -Recurse
    }
    [IO.File]::WriteAllText((Join-Path $artifacts 'SSMT-Gimi-Bridge.dll'), 'bridge fixture')
    [IO.File]::WriteAllText((Join-Path $artifacts 'SSMT-Gimi-Bridge-LICENSE.txt'), 'bridge license fixture')
    $stage = Join-Path $tools 'stage-local-plugins.ps1'
    $pack = Join-Path $tools 'pack-official-plugins.ps1'
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
    Assert-True (@($catalog.entries).Count -eq 2) '正式目录应保留两个获准版本'
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
