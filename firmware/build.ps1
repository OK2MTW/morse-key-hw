# Builds the CwKey firmware for both Black Pill chips (build only, no flashing).
#   .\build.ps1          -> build\cwkey-f411.bin/.uf2, build\cwkey-f401.bin/.uf2 (application at 0x08010000)
#   .\build.ps1 -Test    also builds and runs the host unit tests (needs: python -m pip install ziglang)
# Needs: arm-none-eabi-gcc (ARM_GCC_DIR or PATH), cmake, ninja (python -m pip install cmake ninja), python.
param([switch]$Test)
# Not 'Stop': cmake/ninja print warnings on stderr; failures are checked by exit code.
$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

if (-not $env:ARM_GCC_DIR) {
  $xpack = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\STMicroelectronics\tools\xpack-arm-none-eabi-gcc\*\bin" -ErrorAction SilentlyContinue | Select-Object -Last 1
  if ($xpack) { $env:ARM_GCC_DIR = $xpack.FullName }
}
$scripts = python -c "import sysconfig;print(sysconfig.get_path('scripts','nt_user'))"
$env:PATH = "$scripts;$env:PATH"

python -I tools/get_deps.py
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$cfg = @('-G', 'Ninja', '-B', 'build', '--no-warn-unused-cli', '-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake', '-DCMAKE_BUILD_TYPE=MinSizeRel')
& cmake @cfg 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build build 2>&1 | Select-String -NotMatch 'is not implemented and will always fail|LOAD segment with RWX'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

foreach ($m in 'f411', 'f401') {
  # UF2 family STM32F4 (0x57755a57), application base 0x08010000
  python -I -W ignore ..\tools\uf2\uf2conv.py "build\cwkey-$m.bin" --convert --family 0x57755a57 --base 0x08010000 --output "build\cwkey-$m.uf2" | Out-Null
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  "build\cwkey-$m.uf2"
}

if ($Test) {
  New-Item -ItemType Directory -Force build-host | Out-Null
  python -m ziglang cc -std=gnu11 -Wall -Wextra -Wno-unused-parameter -I src tests/test_host.c src/settings.c src/protocol.c src/keyer.c -o build-host/test_host.exe
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  .\build-host\test_host.exe
  exit $LASTEXITCODE
}
