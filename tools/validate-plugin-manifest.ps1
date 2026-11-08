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

function Assert-TemplatePath($Value, [string]$Context) {
    Assert-String $Value $Context
    if (-not $Value.Contains('${')) {
        Assert-PackagePath $Value $Context
        return
    }
    foreach ($match in [regex]::Matches($Value, '\$\{([^}]+)\}')) {
        $variable = $match.Groups[1].Value
        if ($variable -notmatch '^(plugin\.root|game\.(exe|directory|mods)|external\.[A-Za-z0-9._-]+|service\.(port|token|data)|host\.pid)$') {
            Fail "$Context contains unsupported template variable: $variable"
        }
    }
    if ($Value -match '\$\{[^}]+\}[^/\\].*:') {
        Fail "$Context contains an unsafe templated path"
    }
}

function Assert-Sha256($Value, [string]$Context) {
    Assert-String $Value $Context
    if ($Value -notmatch '^[0-9A-Fa-f]{64}$') { Fail "$Context must be 64 hexadecimal characters" }
}

function Assert-ResourceKey($Value, [string]$Context) {
    Assert-String $Value $Context
    if ($Value.Length -gt 128 -or $Value -notmatch '^[A-Za-z0-9._:-]+$' -or
        $Value.StartsWith(':') -or $Value.EndsWith(':')) {
        Fail "$Context is not a valid resource key"
    }
}

function Assert-LaunchTarget($Value, [string]$Context) {
    Assert-PackagePath $Value $Context
    if ($Value.Replace('\', '/').Split('/') | Where-Object { $_.ToLowerInvariant().StartsWith('.ssmt-') }) {
        Fail "$Context uses a host-reserved path"
    }
}

try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    Fail $_.Exception.Message
}

$topLevel = @(
    'schemaVersion', 'id', 'name', 'version', 'author', 'compatibility',
    'contributions', 'externalDependencies', 'permissions', 'pluginApiVersion'
)
Assert-Object $manifest 'manifest' $topLevel @(
    'schemaVersion', 'id', 'name', 'version', 'author', 'compatibility', 'contributions'
)
if ($manifest.schemaVersion -notin @(1, 2)) { Fail 'schemaVersion must be 1 or 2' }
if ($null -ne $manifest.PSObject.Properties['pluginApiVersion'] -and
    $manifest.pluginApiVersion -isnot [int] -and $manifest.pluginApiVersion -isnot [long]) {
    Fail 'pluginApiVersion must be an integer'
}
Assert-PluginId $manifest.id 'id'
Assert-String $manifest.name 'name'
Assert-String $manifest.version 'version'
if ($manifest.version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
    Fail 'version must be a semantic version'
}
Assert-String $manifest.author 'author'

Assert-Object $manifest.compatibility 'compatibility' @('ssmt', 'pluginApi', 'platforms', 'games') @('ssmt', 'platforms')
Assert-String $manifest.compatibility.ssmt 'compatibility.ssmt'
if ($manifest.schemaVersion -eq 2 -and $null -eq $manifest.compatibility.PSObject.Properties['pluginApi']) {
    Fail 'schemaVersion 2 requires compatibility.pluginApi'
}
if ($null -ne $manifest.compatibility.PSObject.Properties['pluginApi']) {
    Assert-String $manifest.compatibility.pluginApi 'compatibility.pluginApi'
}
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
    'launcherAdapters', 'externalManagers', 'uiPages', 'pages', 'launchHooks',
    'dataDirectories', 'migrations', 'backend'
)

if ($null -ne $contributions.PSObject.Properties['backend']) {
    $backend = $contributions.backend
    Assert-Object $backend 'contributions.backend' @('executable', 'sha256', 'methods', 'events') @('executable', 'sha256')
    Assert-PackagePath $backend.executable 'backend.executable'
    if (-not $backend.executable.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase) -or $backend.sha256 -notmatch '^[0-9a-fA-F]{64}$') { Fail 'invalid backend executable or SHA256' }
    if ('process.spawn' -notin $manifest.permissions) { Fail 'backend requires process.spawn' }
    if ($null -ne $backend.PSObject.Properties['methods']) {
        foreach ($method in (Assert-Array $backend.methods 'backend.methods')) { Assert-PluginId $method 'backend.methods[]' }
        if ($backend.methods.Count -gt 64) { Fail 'too many backend methods' }
    }
    if ($null -ne $backend.PSObject.Properties['events']) {
        foreach ($event in (Assert-Array $backend.events 'backend.events')) {
            if ($event -notin @('prepare', 'cancel', 'disabled', 'recover', 'launch.describe', 'launch.prepare', 'launch.commit', 'launch.cancel')) { Fail 'unsupported backend event' }
        }
        if ($backend.events.Count -gt 8) { Fail 'too many backend events' }
    }
}

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
        Assert-Object $injected 'injectedDlls[]' @('path', 'sha256', 'executableNames') @('path')
        Assert-PackagePath $injected.path 'injectedDlls[].path'
        if ($null -ne $injected.PSObject.Properties['executableNames']) {
            foreach ($game in (Assert-Array $injected.executableNames 'injectedDlls[].executableNames')) { Assert-String $game 'injectedDlls[].executableNames[]' }
        }
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

if ($null -ne $contributions.PSObject.Properties['pages']) {
    $pageIds = @{}
    foreach ($page in (Assert-Array $contributions.pages 'contributions.pages')) {
        Assert-Object $page 'pages[]' @(
            'kind', 'id', 'title', 'description', 'path', 'fields', 'service'
        ) @('kind', 'id', 'title')
        Assert-PluginId $page.id 'pages[].id'
        Assert-String $page.title 'pages[].title'
        if ($pageIds.ContainsKey($page.id)) { Fail "duplicate page id: $($page.id)" }
        $pageIds[$page.id] = $true
        switch ($page.kind) {
            'declarative' {
                foreach ($field in (Assert-Array $page.fields 'pages[].fields')) {
                    Assert-Object $field 'pages[].fields[]' @(
                        'key', 'label', 'kind', 'defaultValue', 'description',
                        'required', 'min', 'max', 'options'
                    ) @('key', 'label', 'kind')
                    Assert-PluginId $field.key 'pages[].fields[].key'
                    Assert-String $field.label 'pages[].fields[].label'
                    if ($field.kind -notin @('boolean', 'integer', 'string', 'select')) {
                        Fail "unsupported declarative field kind: $($field.kind)"
                    }
                    if ($field.kind -eq 'select') {
                        $options = Assert-Array $field.options 'pages[].fields[].options'
                        if (-not $options.Count) { Fail 'select field requires options' }
                        foreach ($option in $options) {
                            Assert-Object $option 'pages[].fields[].options[]' @('value', 'label') @('value', 'label')
                            Assert-String $option.value 'pages[].fields[].options[].value'
                            Assert-String $option.label 'pages[].fields[].options[].label'
                        }
                    }
                }
            }
            'static' {
                Assert-PackagePath $page.path 'pages[].path'
            }
            'local-service' {
                Assert-PackagePath $page.path 'pages[].path'
                Assert-Object $page.service 'pages[].service' @(
                    'executable', 'arguments', 'workingDirectory', 'dataDirectory',
                    'healthPath', 'tokenHeader', 'readyTimeoutMs'
                ) @('executable', 'dataDirectory', 'healthPath')
                Assert-TemplatePath $page.service.executable 'pages[].service.executable'
                if ($null -ne $page.service.PSObject.Properties['arguments']) {
                    foreach ($argument in (Assert-Array $page.service.arguments 'pages[].service.arguments')) {
                        Assert-String $argument 'pages[].service.arguments[]'
                    }
                }
                if ($null -ne $page.service.PSObject.Properties['workingDirectory']) {
                    Assert-TemplatePath $page.service.workingDirectory 'pages[].service.workingDirectory'
                }
                Assert-PackagePath $page.service.dataDirectory 'pages[].service.dataDirectory'
                if ($page.service.dataDirectory.Contains('/') -or $page.service.dataDirectory.Contains('\')) {
                    Fail 'pages[].service.dataDirectory must be one path segment'
                }
                Assert-String $page.service.healthPath 'pages[].service.healthPath'
                if (-not $page.service.healthPath.StartsWith('/') -or
                    $page.service.healthPath.StartsWith('//') -or
                    $page.service.healthPath.Contains('..') -or
                    $page.service.healthPath.Contains('://')) {
                    Fail 'pages[].service.healthPath must be a local absolute URL path'
                }
                if ($null -ne $page.service.PSObject.Properties['tokenHeader']) {
                    Assert-String $page.service.tokenHeader 'pages[].service.tokenHeader'
                    if ($page.service.tokenHeader -notmatch '^[A-Za-z0-9-]+$') {
                        Fail 'pages[].service.tokenHeader is invalid'
                    }
                }
                if ($null -ne $page.service.PSObject.Properties['readyTimeoutMs'] -and
                    ($page.service.readyTimeoutMs -isnot [int] -and $page.service.readyTimeoutMs -isnot [long] -or
                    $page.service.readyTimeoutMs -lt 1 -or $page.service.readyTimeoutMs -gt 120000)) {
                    Fail 'pages[].service.readyTimeoutMs must be between 1 and 120000'
                }
            }
            default { Fail "unsupported page kind: $($page.kind)" }
        }
    }
}

if ($null -ne $contributions.PSObject.Properties['launchHooks']) {
    $hookIds = @{}
    foreach ($hook in (Assert-Array $contributions.launchHooks 'contributions.launchHooks')) {
        Assert-Object $hook 'launchHooks[]' @('id', 'priority', 'actions') @('id', 'actions')
        Assert-PluginId $hook.id 'launchHooks[].id'
        if ($hookIds.ContainsKey($hook.id)) { Fail "duplicate launch hook id: $($hook.id)" }
        $hookIds[$hook.id] = $true
        $actions = Assert-Array $hook.actions 'launchHooks[].actions'
        if (-not $actions.Count -or $actions.Count -gt 64) { Fail 'launchHooks[].actions must contain 1..64 actions' }
        foreach ($action in $actions) {
            Assert-Object $action 'launchHooks[].actions[]' @(
                'type', 'source', 'target', 'sha256', 'resource', 'expectedTargetSha256', 'values'
            ) @('type')
            switch ($action.type) {
                'file-copy' {
                    Assert-PackagePath $action.source 'launchHooks[].actions[].source'
                    Assert-LaunchTarget $action.target 'launchHooks[].actions[].target'
                    Assert-Sha256 $action.sha256 'launchHooks[].actions[].sha256'
                    if ($null -ne $action.PSObject.Properties['expectedTargetSha256']) {
                        Assert-Sha256 $action.expectedTargetSha256 'launchHooks[].actions[].expectedTargetSha256'
                    }
                    if ($null -ne $action.PSObject.Properties['resource']) {
                        Assert-ResourceKey $action.resource 'launchHooks[].actions[].resource'
                    }
                }
                'file-remove' {
                    Assert-LaunchTarget $action.target 'launchHooks[].actions[].target'
                    if ($null -ne $action.PSObject.Properties['expectedTargetSha256']) {
                        Assert-Sha256 $action.expectedTargetSha256 'launchHooks[].actions[].expectedTargetSha256'
                    }
                    if ($null -ne $action.PSObject.Properties['resource']) {
                        Assert-ResourceKey $action.resource 'launchHooks[].actions[].resource'
                    }
                }
                'argument-append' {
                    $values = Assert-Array $action.values 'launchHooks[].actions[].values'
                    if (-not $values.Count -or $values.Count -gt 32) { Fail 'argument-append requires 1..32 values' }
                    foreach ($value in $values) { Assert-String $value 'launchHooks[].actions[].values[]' }
                }
                default { Fail "unsupported launch action type: $($action.type)" }
            }
        }
    }
}

if ($null -ne $contributions.PSObject.Properties['dataDirectories']) {
    $dataIds = @{}
    foreach ($data in (Assert-Array $contributions.dataDirectories 'contributions.dataDirectories')) {
        Assert-Object $data 'dataDirectories[]' @(
            'id', 'packagePath', 'seedFromPackage', 'mirrorOnLaunch'
        ) @('id', 'packagePath')
        Assert-PluginId $data.id 'dataDirectories[].id'
        if ($dataIds.ContainsKey($data.id)) { Fail "duplicate data directory id: $($data.id)" }
        $dataIds[$data.id] = $true
        Assert-PackagePath $data.packagePath 'dataDirectories[].packagePath'
        if ($data.packagePath.Replace('\', '/').ToLowerInvariant().StartsWith('plugin/')) {
            Fail 'dataDirectories[].packagePath must not overlap runtime plugin files'
        }
        foreach ($key in @('seedFromPackage', 'mirrorOnLaunch')) {
            if ($null -ne $data.PSObject.Properties[$key] -and $data.$key -isnot [bool]) {
                Fail "dataDirectories[].$key must be a boolean"
            }
        }
    }
}

if ($null -ne $contributions.PSObject.Properties['migrations']) {
    $migrationIds = @{}
    foreach ($migration in (Assert-Array $contributions.migrations 'contributions.migrations')) {
        Assert-Object $migration 'migrations[]' @('id', 'source', 'keys') @('id', 'source', 'keys')
        Assert-PluginId $migration.id 'migrations[].id'
        if ($migrationIds.ContainsKey($migration.id)) { Fail "duplicate migration id: $($migration.id)" }
        $migrationIds[$migration.id] = $true
        if ($migration.source -ne 'game-config') { Fail "unsupported migration source: $($migration.source)" }
        $keys = Assert-Array $migration.keys 'migrations[].keys'
        if (-not $keys.Count -or $keys.Count -gt 32) { Fail 'migrations[].keys must contain 1..32 keys' }
        foreach ($key in $keys) {
            Assert-String $key 'migrations[].keys[]'
            if ($key -notmatch '^[A-Za-z0-9._-]+$') { Fail "invalid migration key: $key" }
        }
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

$permissionSet = @{}
if ($null -ne $manifest.PSObject.Properties['permissions']) {
    foreach ($permission in @($manifest.permissions)) { $permissionSet[[string]$permission] = $true }
}
if ($null -ne $contributions.PSObject.Properties['pages']) {
    foreach ($page in @($contributions.pages)) {
        if ($page.kind -eq 'local-service' -and -not $permissionSet.ContainsKey('process.spawn')) {
            Fail 'local-service pages require process.spawn permission'
        }
    }
}
if ($null -ne $contributions.PSObject.Properties['launchHooks']) {
    foreach ($hook in @($contributions.launchHooks)) {
        foreach ($action in @($hook.actions)) {
            if ($action.type -eq 'file-copy' -and
                (-not $permissionSet.ContainsKey('filesystem.read') -or -not $permissionSet.ContainsKey('filesystem.write'))) {
                Fail 'file-copy launch actions require filesystem.read and filesystem.write'
            }
            if ($action.type -eq 'file-remove' -and -not $permissionSet.ContainsKey('filesystem.write')) {
                Fail 'file-remove launch actions require filesystem.write'
            }
            if ($action.type -eq 'argument-append' -and -not $permissionSet.ContainsKey('game.launch')) {
                Fail 'argument-append launch actions require game.launch'
            }
        }
    }
}
if ($null -ne $contributions.PSObject.Properties['dataDirectories']) {
    if (-not $permissionSet.ContainsKey('filesystem.read') -or -not $permissionSet.ContainsKey('filesystem.write')) {
        Fail 'dataDirectories require filesystem.read and filesystem.write'
    }
}
if ($null -ne $contributions.PSObject.Properties['migrations']) {
    if (-not $permissionSet.ContainsKey('filesystem.read')) {
        Fail 'migrations require filesystem.read'
    }
}


Write-Output "VALID_MANIFEST=$manifestPath"
