param(
    [Parameter(Mandatory)][string]$LoaderPath,
    [Parameter(Mandatory)][string]$TargetPath,
    [Parameter(Mandatory)][string]$FixtureDllPath
)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-injection-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$children = [Collections.Generic.List[Diagnostics.Process]]::new()

function Assert-True($Value, [string]$Message) {
    if (-not $Value) { throw $Message }
}
function Start-Hidden([string]$Exe, [string]$Arguments, [string]$Directory, [string]$LogName) {
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -WorkingDirectory $Directory `
        -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Directory "$LogName.jsonl") `
        -RedirectStandardError (Join-Path $Directory "$LogName.stderr.log") -PassThru
    $null = $process.Handle
    $children.Add($process)
    return $process
}
function Wait-Loader($Process, [bool]$Success, [string]$Directory) {
    Assert-True ($Process.WaitForExit(25000)) '注入器未在时限内退出'
    $events = @(Get-Content -LiteralPath (Join-Path $Directory 'loader.jsonl') -Encoding UTF8 | ConvertFrom-Json)
    Assert-True (($Process.ExitCode -eq 0) -eq $Success) "退出码不符合预期：$($Process.ExitCode)；日志：$Directory"
    Assert-True (($events.event -contains 'launch_complete') -eq $Success) '错误的成功事件'
    if (-not $Success) {
        Assert-True ($events.event -contains 'launch_error') '失败时缺少结构化错误'
    }
    return $events
}

try {
    foreach ($mode in @('runtime', 'extras', 'host')) {
        $directory = Join-Path $testRoot "$mode 终末地"
        New-Item -ItemType Directory -Path $directory | Out-Null
        $loader = Join-Path $directory 'Run.exe'
        $target = Join-Path $directory 'Target.exe'
        $runtime = Join-Path $directory 'version.dll'
        Copy-Item -LiteralPath $LoaderPath -Destination $loader
        Copy-Item -LiteralPath $TargetPath -Destination $target
        Copy-Item -LiteralPath $FixtureDllPath -Destination $runtime
        $expected = @($runtime)
        $ini = @('[Loader]', "target = $target", 'module = version.dll', 'delay = 1', "launch = $target")
        if ($mode -eq 'runtime') {
            $ini = @('[Loader]', "target = $target", 'module = version.dll', 'delay = -1', "launch = $target")
        }
        $arguments = '--machine-readable --inject-mode direct --wait-for-target --process-timeout 5'
        if ($mode -ne 'runtime') {
            $extraA = Join-Path $directory 'extra-a.dll'
            $extraB = Join-Path $directory 'extra-b.dll'
            Copy-Item -LiteralPath $FixtureDllPath -Destination $extraA
            Copy-Item -LiteralPath $FixtureDllPath -Destination $extraB
            $expected += @($extraA, $extraB)
            $ini += "inject_dlls = $extraA|$extraB"
        }
        if ($mode -eq 'host') {
            $hostDll = Join-Path $directory 'SSMT-PluginHost.dll'
            Copy-Item -LiteralPath $FixtureDllPath -Destination $hostDll
            $configPath = Join-Path $directory 'host.json'
            [IO.File]::WriteAllText($configPath, '{}')
            $arguments += " --plugin-host-config `"$configPath`""
            $expected += $hostDll
        }
        [IO.File]::WriteAllLines((Join-Path $directory 'd3dx.ini'), $ini, [Text.UTF8Encoding]::new($false))
        $process = Start-Hidden $loader $arguments $directory 'loader'
        Start-Sleep -Milliseconds 300
        Assert-True (-not $process.HasExited) "等待模式不应直接启动或完成；日志：$directory"
        $marker = Join-Path $directory 'injected.txt'
        # 同名但路径不同的程序先出现，不能被误注入。
        if ($mode -eq 'runtime') {
            $wrongDirectory = Join-Path $directory 'other'
            New-Item -ItemType Directory -Path $wrongDirectory | Out-Null
            $wrongTarget = Join-Path $wrongDirectory 'Target.exe'
            Copy-Item -LiteralPath $TargetPath -Destination $wrongTarget
            $wrongMarker = Join-Path $wrongDirectory 'wrong.txt'
            $wrong = Start-Hidden $wrongTarget "`"$wrongMarker`" `"$runtime`"" $wrongDirectory 'target'
            Start-Sleep -Milliseconds 300
            Assert-True (-not (Test-Path -LiteralPath $wrongMarker)) '误注入了同名进程'
        }
        $targetArgs = (@($marker) + $expected | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $game = Start-Hidden $target $targetArgs $directory 'target'
        $events = Wait-Loader $process $true $directory
        for ($attempt = 0; $attempt -lt 100 -and -not (Test-Path -LiteralPath $marker); $attempt++) {
            Start-Sleep -Milliseconds 10
        }
        Assert-True (Test-Path -LiteralPath $marker) '目标程序未观察到全部 DLL'
        Assert-True (($events | Where-Object event -eq 'target_detected').pid -eq $game.Id) 'PID 不匹配'
        Assert-True (($events.event -contains 'plugin_host_ready') -eq ($mode -eq 'host')) '插件启停状态不正确'
        Assert-True (-not $game.HasExited) '注入器不应结束外部游戏'
        if ($mode -eq 'runtime') {
            Assert-True (-not (Test-Path -LiteralPath $wrongMarker)) '误注入了同名进程'
            $wrong.Kill()
            $wrong.WaitForExit()
        }
        $game.Kill()
        $game.WaitForExit()
        Write-Host "PASS $mode"
    }

    # 热加载的环境必须由 Run.exe 创建的目标继承，不能依赖父进程或全局设置。
    $directory = Join-Path $testRoot 'hot environment'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $loader = Join-Path $directory 'Run.exe'
    $target = Join-Path $directory 'Target.exe'
    $runtime = Join-Path $directory 'version.dll'
    $marker = Join-Path $directory 'environment.txt'
    Copy-Item -LiteralPath $LoaderPath -Destination $loader
    Copy-Item -LiteralPath $TargetPath -Destination $target
    Copy-Item -LiteralPath $FixtureDllPath -Destination $runtime
    $ini = @('[Loader]', "target = $target", 'module = version.dll', 'delay = 1', "launch = $target", "launch_args = `"$marker`" `"$runtime`"")
    [IO.File]::WriteAllLines((Join-Path $directory 'd3dx.ini'), $ini, [Text.UTF8Encoding]::new($false))
    $process = Start-Hidden $loader '--machine-readable --preload-runtime --set-env "SSMT_FIXTURE_ENV= hot value" --set-env SSMT_FIXTURE_ABSENT=remove --unset-env SSMT_FIXTURE_ABSENT' $directory 'loader'
    $events = Wait-Loader $process $true $directory
    Assert-True ((Get-Content -LiteralPath $marker -Raw) -eq 'injected hot value') '目标未继承指定环境'
    Assert-True (-not $env:SSMT_FIXTURE_ENV) '环境泄漏到测试父进程'
    $game = Get-Process -Id (($events | Where-Object event -eq 'target_created').pid) -ErrorAction SilentlyContinue
    if ($game) { $children.Add($game); $game.Kill(); $game.WaitForExit() }
    Write-Host 'PASS hot environment'

    $directory = Join-Path $testRoot 'failures'
    New-Item -ItemType Directory -Path $directory | Out-Null
    $loader = Join-Path $directory 'Run.exe'
    $target = Join-Path $directory 'Target.exe'
    Copy-Item -LiteralPath $LoaderPath -Destination $loader
    Copy-Item -LiteralPath $TargetPath -Destination $target
    Copy-Item -LiteralPath $FixtureDllPath -Destination (Join-Path $directory 'version.dll')
    $baseIni = @('[Loader]', "target = $target", 'module = version.dll', 'delay = 1')
    foreach ($failure in @('timeout', 'missing-extra', 'invalid-dll', 'invalid-option')) {
        $ini = $baseIni
        $arguments = '--machine-readable --inject-mode direct --wait-for-target --process-timeout 1'
        if ($failure -eq 'missing-extra') { $ini += 'inject_dlls = missing.dll' }
        if ($failure -eq 'invalid-option') { $arguments += ' --process-timeout 99999999999999999999' }
        if ($failure -eq 'invalid-dll') {
            [IO.File]::WriteAllText((Join-Path $directory 'version.dll'), 'invalid DLL')
            $game = Start-Hidden $target "`"$(Join-Path $directory 'invalid.txt')`" `"$(Join-Path $directory 'version.dll')`"" $directory 'target'
        }
        [IO.File]::WriteAllLines((Join-Path $directory 'd3dx.ini'), $ini, [Text.UTF8Encoding]::new($false))
        $process = Start-Hidden $loader $arguments $directory 'loader'
        $null = Wait-Loader $process $false $directory
        if ($failure -eq 'invalid-dll') {
            Assert-True (-not $game.HasExited) '加载失败不应结束外部游戏'
            $game.Kill()
            $game.WaitForExit()
        }
        Write-Host "PASS $failure"
    }
    Write-Host "日志与夹具：$testRoot"
} finally {
    foreach ($child in $children) {
        if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
        $child.Dispose()
    }
}
