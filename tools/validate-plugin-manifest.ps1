#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'
$manifestPath = [IO.Path]::GetFullPath($Path)

function Fail([string]$Message) {
    throw "invalid plugin manifest ($manifestPath): $Message"
}

function Assert-Object($Value, [string]$Context, [string[]]$Allowed, [string[]]$Required = @()) {
    if ($Value -isnot [System.Management.Automation.PSCustomObject]) {
        Fail "$Context must be an object"
    }
    $names = @($Value.PSObject.Properties.Name)
    $unknown = @($names | Where-Object { $_ -notin $Allowed })
    if ($unknown.Count) {
        Fail "$Context contains unsupported field(s): $($unknown -join ', ')"
    }
    $missing = @($Required | Where-Object { $_ -notin $names })
    if ($missing.Count) {
        Fail "$Context is missing required field(s): $($missing -join ', ')"
    }
}

function Assert-String($Value, [string]$Context, [bool]$AllowEmpty = $false) {
    if ($Value -isnot [string] -or (-not $AllowEmpty -and [string]::IsNullOrWhiteSpace($Value))) {
        Fail "$Context must be a non-empty string"
    }
}

function Assert-Array($Value, [string]$Context) {
    if ($Value -is [string] -or $Value -isnot [System.Collections.IList]) {
        Fail "$Context must be an array"
    }
    return @($Value)
}

function Assert-PluginId($Value, [string]$Context) {
    Assert-String $Value $Context
    if ($Value.Length -gt 128 -or $Value -notmatch '^[A-Za-z0-9._-]+$' -or
        $Value.StartsWith('.') -or $Value.EndsWith('.')) {
        Fail "$Context is not a valid plugin ID"
    }
}

function Assert-PackagePath($Value, [string]$Context) {
    Assert-String $Value $Context
    if ($Value.Contains([char]0) -or $Value.StartsWith('/') -or $Value.StartsWith('\') -or
        ($Value.Length -ge 2 -and $Value[1] -eq ':')) {
        Fail "$Context must be a package-relative path"
    }
    foreach ($segment in $Value.Replace('\', '/').Split('/')) {
        $reservedStem = $segment.Split('.')[0].ToUpperInvariant()
        $reserved = $reservedStem -in @('CON', 'PRN', 'AUX', 'NUL') -or
            ($reservedStem.Length -eq 4 -and $reservedStem -match '^(COM|LPT)[1-9]$')
        if ([string]::IsNullOrEmpty($segment) -or $segment -in @('.', '..') -or
            $segment.EndsWith('.') -or $segment.EndsWith(' ') -or
            $segment.IndexOfAny([char[]]'<>:"|?*') -ge 0 -or $reserved) {
            Fail "$Context contains an unsafe path segment: $segment"
        }
    }
}

try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    Fail $_.Exception.Message
}

$topLevel = @(
    'schemaVersion', 'id', 'name', 'version', 'author', 'compatibility',
    'contributions', 'externalDependencies', 'permissions'
)
Assert-Object $manifest 'manifest' $topLevel @(
    'schemaVersion', 'id', 'name', 'version', 'author', 'compatibility', 'contributions'
)
if ($manifest.schemaVersion -ne 1) { Fail 'schemaVersion must be 1' }
Assert-PluginId $manifest.id 'id'
Assert-String $manifest.name 'name'
Assert-String $manifest.version 'version'
if ($manifest.version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    Fail 'version must be a semantic version'
}
Assert-String $manifest.author 'author'

Assert-Object $manifest.compatibility 'compatibility' @('ssmt', 'platforms', 'games') @('ssmt', 'platforms')
Assert-String $manifest.compatibility.ssmt 'compatibility.ssmt'
$platforms = Assert-Array $manifest.compatibility.platforms 'compatibility.platforms'
if (-not $platforms.Count) { Fail 'compatibility.platforms must not be empty' }
if (@($platforms | Select-Object -Unique).Count -ne $platforms.Count) {
    Fail 'compatibility.platforms contains duplicates'
}
foreach ($platform in $platforms) {
    if ($platform -ne 'windows-x64') { Fail "unsupported platform: $platform" }
}
if ($null -ne $manifest.compatibility.PSObject.Properties['games']) {
    $games = Assert-Array $manifest.compatibility.games 'compatibility.games'
    foreach ($game in $games) { Assert-String $game 'compatibility.games[]' }
}

$contributions = $manifest.contributions
Assert-Object $contributions 'contributions' @(
    'hostExtensions', 'runtimePlugins', 'injectedDlls',
    'launcherAdapters', 'externalManagers', 'uiPages'
)

if ($null -ne $contributions.PSObject.Properties['hostExtensions']) {
    foreach ($extension in (Assert-Array $contributions.hostExtensions 'contributions.hostExtensions')) {
        Assert-Object $extension 'contributions.hostExtensions[]' @(
            'id', 'provider', 'priority', 'config', 'settingsComponent'
        ) @('id', 'provider')
        Assert-PluginId $extension.id 'hostExtensions[].id'
        Assert-PluginId $extension.provider 'hostExtensions[].provider'
        if ($null -ne $extension.PSObject.Properties['priority'] -and
            $extension.priority -isnot [int] -and $extension.priority -isnot [long]) {
            Fail 'hostExtensions[].priority must be an integer'
        }
        if ($null -ne $extension.PSObject.Properties['config']) {
            Assert-PackagePath $extension.config 'hostExtensions[].config'
        }
        if ($null -ne $extension.PSObject.Properties['settingsComponent']) {
            Assert-PluginId $extension.settingsComponent 'hostExtensions[].settingsComponent'
        }
    }
}

if ($null -ne $contributions.PSObject.Properties['runtimePlugins']) {
    foreach ($runtime in (Assert-Array $contributions.runtimePlugins 'contributions.runtimePlugins')) {
        Assert-Object $runtime 'runtimePlugins[]' @('path') @('path')
        Assert-PackagePath $runtime.path 'runtimePlugins[].path'
    }
}

if ($null -ne $contributions.PSObject.Properties['injectedDlls']) {
    foreach ($injected in (Assert-Array $contributions.injectedDlls 'contributions.injectedDlls')) {
        Assert-Object $injected 'injectedDlls[]' @('path', 'sha256') @('path')
        Assert-PackagePath $injected.path 'injectedDlls[].path'
        if (-not $injected.path.ToLowerInvariant().EndsWith('.dll')) {
            Fail 'injectedDlls[].path must end with .dll'
        }
        if ($null -ne $injected.PSObject.Properties['sha256'] -and
            $injected.sha256 -notmatch '^[0-9A-Fa-f]{64}$') {
            Fail 'injectedDlls[].sha256 must be 64 hexadecimal characters'
        }
    }
}

if ($null -ne $contributions.PSObject.Properties['launcherAdapters']) {
    foreach ($adapter in (Assert-Array $contributions.launcherAdapters 'contributions.launcherAdapters')) {
        Assert-Object $adapter 'launcherAdapters[]' @(
            'type', 'executable', 'arguments', 'workingDirectory', 'ready', 'failurePolicy'
        ) @('type', 'executable')
        if ($adapter.type -ne 'process-forwarder') { Fail "unsupported launcher adapter type: $($adapter.type)" }
        Assert-PackagePath $adapter.executable 'launcherAdapters[].executable'
    }
}

if ($null -ne $contributions.PSObject.Properties['externalManagers']) {
    foreach ($manager in (Assert-Array $contributions.externalManagers 'contributions.externalManagers')) {
        Assert-Object $manager 'externalManagers[]' @('id', 'executable', 'workingDirectory') @('id', 'executable')
        Assert-PluginId $manager.id 'externalManagers[].id'
        Assert-PackagePath $manager.executable 'externalManagers[].executable'
    }
}

if ($null -ne $contributions.PSObject.Properties['uiPages']) {
    foreach ($page in (Assert-Array $contributions.uiPages 'contributions.uiPages')) {
        Assert-Object $page 'uiPages[]' @('id', 'route', 'path') @('id', 'route', 'path')
        Assert-PluginId $page.id 'uiPages[].id'
        Assert-String $page.route 'uiPages[].route'
        Assert-PackagePath $page.path 'uiPages[].path'
    }
}

if ($null -ne $manifest.PSObject.Properties['externalDependencies']) {
    foreach ($dependency in (Assert-Array $manifest.externalDependencies 'externalDependencies')) {
        Assert-Object $dependency 'externalDependencies[]' @(
            'id', 'type', 'requiredFiles', 'requiredFor', 'downloadUrl', 'downloadHint'
        ) @('id', 'type')
        Assert-PluginId $dependency.id 'externalDependencies[].id'
        if ($dependency.type -ne 'directory') { Fail "unsupported external dependency type: $($dependency.type)" }
        if ($null -ne $dependency.PSObject.Properties['requiredFiles']) {
            foreach ($file in (Assert-Array $dependency.requiredFiles 'externalDependencies[].requiredFiles')) {
                Assert-PackagePath $file 'externalDependencies[].requiredFiles[]'
            }
        }
        if ($null -ne $dependency.PSObject.Properties['requiredFor']) {
            foreach ($feature in (Assert-Array $dependency.requiredFor 'externalDependencies[].requiredFor')) {
                Assert-String $feature 'externalDependencies[].requiredFor[]'
            }
        }
        if ($null -ne $dependency.PSObject.Properties['downloadUrl']) {
            Assert-String $dependency.downloadUrl 'externalDependencies[].downloadUrl'
            $url = [Uri]$dependency.downloadUrl
            if ($url.Scheme -ne 'https' -or $url.UserInfo) {
                Fail 'externalDependencies[].downloadUrl must be credential-free HTTPS'
            }
        }
        if ($null -ne $dependency.PSObject.Properties['downloadHint']) {
            Assert-String $dependency.downloadHint 'externalDependencies[].downloadHint'
        }
    }
}

if ($null -ne $manifest.PSObject.Properties['permissions']) {
    $allowedPermissions = @(
        'process.spawn', 'process.observe', 'game.launch',
        'filesystem.read', 'filesystem.write', 'native.inject', 'network'
    )
    $permissions = Assert-Array $manifest.permissions 'permissions'
    if (@($permissions | Select-Object -Unique).Count -ne $permissions.Count) {
        Fail 'permissions contains duplicates'
    }
    foreach ($permission in $permissions) {
        if ($permission -notin $allowedPermissions) { Fail "unsupported permission: $permission" }
    }
}

Write-Output "VALID_MANIFEST=$manifestPath"
