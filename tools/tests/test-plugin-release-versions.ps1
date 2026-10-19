#Requires -Version 7.0
$ErrorActionPreference = 'Stop'
$check = Join-Path $PSScriptRoot '../check-plugin-release-versions.ps1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-plugin-version-test-' + [Guid]::NewGuid().ToString('N'))
function Write-Fixture([string]$Path, [string]$Content) {
    $destination = Join-Path $testRoot $Path
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    [IO.File]::WriteAllText($destination, $Content)
}
function Git-Fixture([string[]]$Arguments) {
    & git -C $testRoot @Arguments
    if ($LASTEXITCODE -ne 0) { throw "测试 Git 命令失败：$($Arguments -join ' ')" }
}
function Reject([string]$Reason) {
    $errorMessage = ''
    try { & $check -BaseRef HEAD -RepositoryRoot $testRoot }
    catch { $errorMessage = $_.Exception.Message }
    if ($errorMessage -notlike "*$Reason*") { throw "预期拒绝 $Reason，实际：$errorMessage" }
}
try {
    [IO.Directory]::CreateDirectory($testRoot) | Out-Null
    Git-Fixture @('init', '--quiet')
    $package = 'official-plugin-packages/example.poser/1.0.0'
    $status = '{"publish":true,"sourcePaths":["source","shared"]}'
    Write-Fixture "$package/ssmt-plugin.json" '{"id":"example.poser","version":"1.0.0"}'
    Write-Fixture "$package/release-status.json" $status
    Write-Fixture 'source/plugin.cpp' 'original source'
    Write-Fixture 'shared/dependency.h' 'original dependency'
    Write-Fixture 'other/unrelated.cpp' 'unrelated source'
    Git-Fixture @('add', '.')
    Git-Fixture @('-c', 'user.name=SSMT Test', '-c', 'user.email=ssmt-test@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', '插件发布检查夹具')
    & $check -BaseRef HEAD -RepositoryRoot $testRoot
    Write-Fixture 'other/unrelated.cpp' 'unrelated update'
    & $check -BaseRef HEAD -RepositoryRoot $testRoot
    Write-Fixture 'source/plugin.cpp' 'changed source'
    Reject '版本仍为'
    Write-Fixture 'source/plugin.cpp' 'original source'
    Write-Fixture 'shared/dependency.h' 'changed dependency'
    Reject '版本仍为'
    Write-Fixture 'shared/dependency.h' 'original dependency'
    Write-Fixture "$package/release-status.json" '{"publish":true}'
    Reject '不得移除'
    Write-Fixture "$package/release-status.json" '{"publish":true,"sourcePaths":["../escape"]}'
    Reject '无效的插件源码路径'
    Write-Fixture "$package/release-status.json" $status
    Write-Fixture 'source/plugin.cpp' 'changed source'
    Write-Fixture 'official-plugin-packages/example.poser/1.0.1/ssmt-plugin.json' '{"id":"example.poser","version":"1.0.1"}'
    Write-Fixture 'official-plugin-packages/example.poser/1.0.1/release-status.json' $status
    & $check -BaseRef HEAD -RepositoryRoot $testRoot
    Write-Fixture 'official-plugin-packages/example.poser/1.0.1/release-status.json' '{"publish":false}'
    Reject '版本仍为'
    Write-Fixture "$package/release-status.json" '{"publish":false}'
    Write-Fixture 'official-plugin-packages/example.poser/0.9.0/ssmt-plugin.json' '{"id":"example.poser","version":"0.9.0"}'
    Write-Fixture 'official-plugin-packages/example.poser/0.9.0/release-status.json' $status
    Reject '不得低于'
    Write-Host '插件发布版本回归通过：未改源码、无关改动、源码及共享依赖变更、路径校验、正式升级与版本回退。'
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($resolved.StartsWith($parent + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolved) -match '^ssmt-plugin-version-test-[0-9a-f]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
