param(
    [string]$ArduinoCli = (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
    [switch]$SkipDefaultSMS,
    [string[]]$OnlyProfile = @(),
    [ValidateRange(1, 8)][int]$Jobs = 2
)
$ErrorActionPreference = 'Stop'
$projectPath = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$buildFixtureRoot = Join-Path $env:TEMP ('djua-profiles-' + [Guid]::NewGuid().ToString('N'))
$originalConfig = [IO.File]::ReadAllText((Join-Path $projectPath 'src\config.h'))
$profiles = @(
    @{ Name = 'SMS_PRODUCTION'; Profile = 1; Defines = '' },
    @{ Name = 'MQTT_TEST'; Profile = 2; Defines = '' },
    @{ Name = 'HTTP_TEST'; Profile = 3; Defines = '' },
    # Compilation uniquement : PWRKEY et shunts restent des fixtures.
    @{ Name = 'SMS_UART_PROVISIONING_COMPILE_ONLY'; Profile = 1; Defines = "#define GSM_HARDWARE_CONFIGURED 1`n#define D2_PROVISIONING_ENABLED 1`n#define V2_BATTERY_CONFIGURED 1`n#define V2_SOLAR_CONFIGURED 1`n#define V2_DC_CONFIGURED 1`n#define V2_TV_CONFIGURED 1`n#define PZEM_HARDWARE_CONFIGURED 1`n#define D3_RECEIVER_ENABLED 1`n" },
    @{ Name = 'SMS_DC_DISABLED'; Profile = 1; Defines = "#define V2_DC_CONFIGURED 0`n#define V2_TV_CONFIGURED 0`n" }
)
try {
    $fixturePath = Join-Path $buildFixtureRoot 'iot_box'
    $null = New-Item -ItemType Directory -Path $fixturePath -Force
    Copy-Item -LiteralPath (Join-Path $projectPath 'src') -Destination $fixturePath -Recurse
    Copy-Item -LiteralPath (Join-Path $projectPath 'iot_box.ino') -Destination $fixturePath
foreach ($profile in $profiles) {
    if ($OnlyProfile.Count -gt 0 -and $profile.Name -notin $OnlyProfile) { continue }
    if ($SkipDefaultSMS -and $profile.Name -eq 'SMS_PRODUCTION') { continue }
    Write-Host "Verification $($profile.Name) (aucun flash)"
    # Copies de compilation temporaires : le depot utilisateur reste inchange.
    # Les defines vont dans le config.h copie, commun au .ino et a TOUS les .cpp.
    # Cela reutilise le cache du coeur Arduino au lieu de le reconstruire 4 fois.
    Copy-Item -LiteralPath (Join-Path $projectPath 'src\pins.h') -Destination (Join-Path $fixturePath 'src\pins.h') -Force
    $config = $originalConfig.Replace('#define DJUA_COMM_PROFILE DJUA_PROFILE_SMS_PRODUCTION',
        "#define DJUA_COMM_PROFILE $($profile.Profile)")
    $config = $config.Replace('#define CONFIG_H', "#define CONFIG_H`n$($profile.Defines)")
    [IO.File]::WriteAllText((Join-Path $fixturePath 'src\config.h'), $config)
    if ($profile.Name -eq 'SMS_UART_PROVISIONING_COMPILE_ONLY') {
        $pinsFile = Join-Path $fixturePath 'src\pins.h'
        $pins = [IO.File]::ReadAllText($pinsFile).Replace('#define GSM_PWRKEY_PIN -1', '#define GSM_PWRKEY_PIN 4')
        [IO.File]::WriteAllText($pinsFile, $pins)
    }
    & $ArduinoCli compile --fqbn esp32:esp32:esp32 --warnings all --jobs $Jobs `
        --build-path (Join-Path $buildFixtureRoot 'build') $fixturePath
    if ($LASTEXITCODE -ne 0) { throw "Compilation echouee : $($profile.Name)" }
}
} finally {
    # Remove only this invocation's generated fixture/build, never user caches.
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    $resolvedFixture = [IO.Path]::GetFullPath($buildFixtureRoot)
    if (-not $resolvedFixture.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedFixture) -notmatch '^djua-profiles-[0-9a-f]{32}$') {
        throw 'Chemin temporaire de nettoyage invalide'
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
Write-Host 'Profils verifies ; copies temporaires nettoyees.'
