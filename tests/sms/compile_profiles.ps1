param(
    [string]$ArduinoCli = (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
    [switch]$SkipDefaultSMS,
    [string[]]$OnlyProfile = @()
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
    @{ Name = 'SMS_UART_PROVISIONING_COMPILE_ONLY'; Profile = 1; Defines = "#define GSM_HARDWARE_CONFIGURED 1`n#define D2_PROVISIONING_ENABLED 1`n#define DC_LOAD_SHUNT_CONFIGURED 1`n#define SOLAR_MEASUREMENT_ENABLED 1`n" },
    @{ Name = 'SMS_DC_DISABLED'; Profile = 1; Defines = "#define DC_LOAD_MEASUREMENT_ENABLED 0`n" }
)
foreach ($profile in $profiles) {
    if ($OnlyProfile.Count -gt 0 -and $profile.Name -notin $OnlyProfile) { continue }
    if ($SkipDefaultSMS -and $profile.Name -eq 'SMS_PRODUCTION') { continue }
    Write-Host "Verification $($profile.Name) (aucun flash)"
    # Copies de compilation temporaires : le depot utilisateur reste inchange.
    # Les defines vont dans le config.h copie, commun au .ino et a TOUS les .cpp.
    # Cela reutilise le cache du coeur Arduino au lieu de le reconstruire 4 fois.
    $fixturePath = Join-Path (Join-Path $buildFixtureRoot $profile.Name) 'iot_box'
    $null = New-Item -ItemType Directory -Path $fixturePath -Force
    Copy-Item -LiteralPath (Join-Path $projectPath 'src') -Destination $fixturePath -Recurse
    Copy-Item -LiteralPath (Join-Path $projectPath 'iot_box.ino') -Destination $fixturePath
    $config = $originalConfig.Replace('#define DJUA_COMM_PROFILE DJUA_PROFILE_SMS_PRODUCTION',
        "#define DJUA_COMM_PROFILE $($profile.Profile)")
    $config = $config.Replace('#define CONFIG_H', "#define CONFIG_H`n$($profile.Defines)")
    [IO.File]::WriteAllText((Join-Path $fixturePath 'src\config.h'), $config)
    if ($profile.Name -eq 'SMS_UART_PROVISIONING_COMPILE_ONLY') {
        $pinsFile = Join-Path $fixturePath 'src\pins.h'
        $pins = [IO.File]::ReadAllText($pinsFile).Replace('#define GSM_PWRKEY_PIN -1', '#define GSM_PWRKEY_PIN 14')
        [IO.File]::WriteAllText($pinsFile, $pins)
    }
    & $ArduinoCli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 `
        $fixturePath
    if ($LASTEXITCODE -ne 0) { throw "Compilation echouee : $($profile.Name)" }
}
Write-Host "Copies de verification conservees dans $buildFixtureRoot"
