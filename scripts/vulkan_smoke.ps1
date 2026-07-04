param(
    [string] $BuildDir = "build/dev-mswin",
    [int] $TimeoutSeconds = 30,
    [string] $LogDir = "",
    [switch] $IncludePhaseExamples
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repo = Resolve-Path (Join-Path $PSScriptRoot "..")
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    Resolve-Path $BuildDir
} else {
    Resolve-Path (Join-Path $repo $BuildDir)
}
$bin = Join-Path $build "bin"
if (-not (Test-Path $bin)) {
    throw "Runtime bin directory not found: $bin"
}

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $LogDir = Join-Path $build "vulkan-example-smoke-30s"
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$badLogPattern = "Validation Error|VK_ERROR|validation layer|VUID-"
$results = New-Object System.Collections.Generic.List[object]

function Invoke-Smoke {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [string[]] $ExeArgs = @(),
        [string] $InputText = "",
        [bool] $Required = $true,
        [int] $RequiredSelfcheckPasses = 0,
        [string] $RequiredPassPattern = "",
        [int] $RequiredPasses = 0
    )

    $exe = Join-Path $bin ($Name + ".exe")
    if (-not (Test-Path $exe)) {
        $status = if ($Required) { "missing" } else { "skipped-missing" }
        $results.Add([pscustomobject]@{
            Name = $Name
            Status = $status
            ExitCode = $null
            BadLog = $false
            MissingSelfcheck = $false
            SelfcheckPasses = 0
            Log = $null
        })
        return
    }

    $outLog = Join-Path $LogDir ($Name + ".out.log")
    $errLog = Join-Path $LogDir ($Name + ".err.log")
    Remove-Item -Force -ErrorAction SilentlyContinue $outLog, $errLog

    $start = @{
        FilePath = $exe
        WorkingDirectory = $repo
        RedirectStandardOutput = $outLog
        RedirectStandardError = $errLog
        PassThru = $true
        WindowStyle = "Hidden"
    }
    [string[]] $normalizedArgs = @()
    if ($null -ne $ExeArgs) {
        $normalizedArgs = [string[]] $ExeArgs
    }
    if ($normalizedArgs.Length -gt 0) {
        $start.ArgumentList = $normalizedArgs
    }
    $inputPath = $null
    if ($InputText.Length -gt 0) {
        $inputPath = Join-Path $LogDir ($Name + ".stdin.txt")
        Set-Content -Path $inputPath -Value $InputText -NoNewline -Encoding ASCII
        $start.RedirectStandardInput = $inputPath
    }

    $effectiveTimeoutSeconds = if ($RequiredSelfcheckPasses -gt 0 -or $RequiredPasses -gt 0) {
        [Math]::Max($TimeoutSeconds, 10)
    } else {
        $TimeoutSeconds
    }

    $proc = Start-Process @start
    $timedOut = -not $proc.WaitForExit($effectiveTimeoutSeconds * 1000)
    if ($timedOut) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $proc.WaitForExit()
    } else {
        $proc.WaitForExit()
    }
    $proc.Refresh()
    $exitCode = if ($timedOut) { $null } else { $proc.ExitCode }

    $text = ""
    if (Test-Path $outLog) { $text += Get-Content -Raw $outLog }
    if (Test-Path $errLog) { $text += Get-Content -Raw $errLog }
    $selfcheckPasses = 0
    $missingSelfcheck = $false
    if ($RequiredSelfcheckPasses -gt 0) {
        $selfcheckPattern = "\[" + [regex]::Escape($Name) + "\] selfcheck .* pass=1"
        $selfcheckPasses = [regex]::Matches($text, $selfcheckPattern).Count
        $missingSelfcheck = $selfcheckPasses -lt $RequiredSelfcheckPasses
    }
    if ($RequiredPasses -gt 0) {
        $patternPasses = [regex]::Matches($text, $RequiredPassPattern).Count
        $selfcheckPasses = [Math]::Max($selfcheckPasses, $patternPasses)
        $missingSelfcheck = $missingSelfcheck -or $patternPasses -lt $RequiredPasses
    }
    if (($RequiredSelfcheckPasses -gt 0 -or $RequiredPasses -gt 0) -and
        (-not $timedOut) -and $null -eq $exitCode -and -not $missingSelfcheck) {
        $exitCode = 0
    }
    $badLog = $text -match $badLogPattern
    $status = if ($timedOut) { "timeout" } else { "exit" }

    $results.Add([pscustomobject]@{
        Name = $Name
        Status = $status
        ExitCode = $exitCode
        BadLog = $badLog
        MissingSelfcheck = $missingSelfcheck
        SelfcheckPasses = $selfcheckPasses
        Log = $LogDir
    })
}

$gltfData = Join-Path $repo "data/models/gltf"
$argMap = @{
    "vulkan_event_camera" = @("--selfcheck")
    "vulkan_gltf_samples" = @($gltfData)
    "vulkan_lidar" = @("--selfcheck")
}
$selfcheckMap = @{
    "vulkan_event_camera" = 1
    "vulkan_lidar" = 2
}

$vulkanExamples = @(Get-ChildItem -Path $bin -Filter "vulkan_*.exe" |
    ForEach-Object { $_.BaseName } |
    Sort-Object)

if ($vulkanExamples.Length -eq 0) {
    throw "No vulkan_*.exe examples found in $bin"
}

foreach ($name in $vulkanExamples) {
    $argsForExample = if ($argMap.ContainsKey($name)) { $argMap[$name] } else { @() }
    $requiredSelfchecks = if ($selfcheckMap.ContainsKey($name)) { $selfcheckMap[$name] } else { 0 }
    Invoke-Smoke -Name $name -ExeArgs $argsForExample -RequiredSelfcheckPasses $requiredSelfchecks
}

if ($IncludePhaseExamples) {
    $phaseExamples = @(
        "texture2d", "data_texture", "cubemap", "hdr_envmap", "depth_texture",
        "SpheroControl", "basic_geometries", "geometries", "dynamic",
        "instancing", "points", "sprite", "text_sprite", "morphtargets",
        "morphtargets_sphere", "bones", "particle_system", "hemi_light",
        "directional", "point_light", "spot_light", "rect_area_light",
        "clipping", "transmission", "fonts", "catmull_room_curve3",
        "cubic_bezier_curve", "spline_editor", "raw_shader", "seascape_demo",
        "water", "depth_sensor", "lidar", "lidar_slam", "forest_demo",
        "drive"
    )
    foreach ($name in $phaseExamples) {
        Invoke-Smoke -Name $name -InputText "4`n" -Required $false
    }

    Invoke-Smoke -Name "robot_cell" -ExeArgs @("--depthprobe", "vulkan") -Required $false -RequiredPassPattern "frame \d+: .* OK" -RequiredPasses 5
    Invoke-Smoke -Name "tps_shooter" -ExeArgs @("--shot", (Join-Path $LogDir "vulkan_final_shooter.png"), "--frames", "180") -InputText "4`n" -Required $false
}

$failures = @(
    $results | Where-Object {
        $_.BadLog -or
        $_.MissingSelfcheck -or
        $_.Status -eq "missing" -or
        ($_.Status -eq "exit" -and $_.ExitCode -ne 0)
    }
)

$results | Format-Table -AutoSize
$results | ConvertTo-Json -Depth 4 | Set-Content -Path (Join-Path $LogDir "summary.json") -Encoding UTF8

if ($failures.Length -gt 0) {
    Write-Error ("Vulkan smoke failed: " + (($failures | ForEach-Object { $_.Name }) -join ", "))
}
