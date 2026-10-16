param(
    [Parameter(Mandatory)][string]$LoaderPath,
    [Parameter(Mandatory)][string]$TargetPath,
    [Parameter(Mandatory)][string]$RuntimePath,
    [string]$ReShadePath
)
$ErrorActionPreference = 'Stop'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('ssmt-runtime-graphics-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$loader = Join-Path $testRoot 'Run.exe'
$game = Join-Path $testRoot 'game'
New-Item -ItemType Directory -Path $game | Out-Null
$target = Join-Path $game 'Target.exe'
$runtime = Join-Path $testRoot 'd3d11.dll'
$marker = Join-Path $testRoot 'graphics.txt'
Copy-Item -LiteralPath $LoaderPath -Destination $loader
Copy-Item -LiteralPath $TargetPath -Destination $target
Copy-Item -LiteralPath $RuntimePath -Destination $runtime
$ini = @('[Loader]', "target=$target", 'module=d3d11.dll', 'delay=0', "launch=$target",
    "launch_args=`"$marker`" --probe-d3d11", '[System]', 'load_library_redirect=2',
    '[Device]', 'allow_platform_update=1', '[Logging]', 'calls=1', 'unbuffered=1')
if ($ReShadePath) { $ini = @($ini[0], "inject_dlls=$ReShadePath") + $ini[1..($ini.Length - 1)] }
[IO.File]::WriteAllLines((Join-Path $testRoot 'd3dx.ini'), $ini, [Text.UTF8Encoding]::new($false))
$process = Start-Process -FilePath $loader -ArgumentList '--machine-readable --preload-runtime' `
    -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $testRoot 'loader.jsonl') -RedirectStandardError (Join-Path $testRoot 'loader.stderr.log')
try {
    $null = $process.Handle
    if (-not $process.WaitForExit(25000) -or $process.ExitCode -ne 0) { throw "注入器失败：$testRoot" }
    $events = @(Get-Content (Join-Path $testRoot 'loader.jsonl') -Encoding UTF8 | ConvertFrom-Json)
    $pidEvent = $events | Where-Object event -eq 'target_created' | Select-Object -First 1
    $targetProcess = Get-Process -Id $pidEvent.pid -ErrorAction SilentlyContinue
    if ($targetProcess) { $null = $targetProcess.WaitForExit(15000) }
    if (-not (Test-Path -LiteralPath $marker) -or (Get-Content -LiteralPath $marker -Raw) -ne 'runtime wrapped d3d11') {
        throw "未通过真实 Runtime 包装身份验证：$testRoot"
    }
    Write-Host "PASS delayed system D3D11: $testRoot"
} finally {
    # 只结束本夹具创建的进程，保留日志用于核对本次加载顺序。
    if ($targetProcess -and -not $targetProcess.HasExited) { $targetProcess.Kill(); $targetProcess.WaitForExit() }
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
