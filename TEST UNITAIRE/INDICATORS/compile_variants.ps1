param([string]$ArduinoCli = 'arduino-cli')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path.Replace('\', '/')
$source = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'indicators_test/indicators_test.ino'))
$source = $source.Replace('../../../src/', "$repo/src/")
$sketch = Join-Path $env:TEMP 'djua-indicators-variants/indicators_test'
$null = New-Item -ItemType Directory -Force -Path $sketch
foreach ($variant in @(@(0, 0), @(1, 0), @(1, 1))) {
    $enabled = $variant[0]
    $passive = $variant[1]
    Write-Host "ESP32: BUZZER_ENABLED=$enabled BUZZER_PASSIVE=$passive"
    # Les defines locaux ne forcent pas une recompilation de tout le coeur.
    $prefix = "#define BUZZER_ENABLED $enabled`n#define BUZZER_PASSIVE $passive`n"
    [IO.File]::WriteAllText((Join-Path $sketch 'indicators_test.ino'), $prefix + $source)
    & $ArduinoCli compile --fqbn esp32:esp32:esp32 --warnings all --jobs 1 $sketch
    if ($LASTEXITCODE -ne 0) { throw "Echec compilation enabled=$enabled passive=$passive" }
}
