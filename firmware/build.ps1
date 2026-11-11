# Builds the CwKey firmware for WeAct Black Pill (build only, no flashing).
#   .\build.ps1 [-Mcu F411|F401] [-Uf2]
#     -Mcu  chip on the board (the images are not interchangeable: RAM size, clock)
#     -Uf2  image for 0x08010000 behind the TinyUF2 bootloader + .uf2 file;
#           without it the image is for 0x08000000 (ROM DFU or ST-Link, no bootloader)
# One firmware drives both sidetone outputs (active buzzer on PB8, speaker on PA8).
# Output: CwKey\build-<mcu>[-uf2]\  (CwKey.ino.bin, CwKey-<MCU>.uf2)
param(
  [ValidateSet('F411', 'F401')][string]$Mcu = 'F411',
  [switch]$Uf2
)

$ErrorActionPreference = 'Stop'
$cli = 'C:\Program Files\Arduino CLI\arduino-cli.exe'
$sketch = Join-Path $PSScriptRoot 'CwKey'

# F401: the BLACKPILL_F401CE variant has the right RAM (96 KB) for F401xD/E; flash is
# limited to 384 KB because the F401xD fitted on some boards has no more.
$boards = @{
  'F411' = @{ pnum = 'BLACKPILL_F411CE'; flash = 524288 }
  'F401' = @{ pnum = 'BLACKPILL_F401CE'; flash = 393216 }
}
$b = $boards[$Mcu]
$fqbn = "STMicroelectronics:stm32:GenF4:pnum=$($b.pnum),usb=HID,upload_method=dfuMethod"
# Host polls the HID endpoint every 1 ms instead of 10 ms (H-30).
$flags = '-DHID_FS_BINTERVAL=1'

$props = @(
  '--build-property', "compiler.c.extra_flags=$flags",
  '--build-property', "compiler.cpp.extra_flags=$flags"
)
if ($Uf2) {
  # TinyUF2 occupies sectors 0-3 (64 KB); the application starts at 0x08010000.
  $out = Join-Path $sketch "build-$($Mcu.ToLower())-uf2"
  $props += @(
    '--build-property', 'build.flash_offset=0x10000',
    '--build-property', "upload.maximum_size=$($b.flash - 65536)"
  )
} else {
  $out = Join-Path $sketch "build-$($Mcu.ToLower())"
  $props += @('--build-property', "upload.maximum_size=$($b.flash)")
}

& $cli compile --fqbn $fqbn --output-dir $out --build-path (Join-Path $out 'obj') @props $sketch
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Uf2) {
  $conv = Join-Path $PSScriptRoot '..\tools\uf2\uf2conv.py'
  $uf2File = Join-Path $out "CwKey-$Mcu.uf2"
  # Family STM32F4 (0x57755a57), base address of the application.
  python -I -W ignore $conv (Join-Path $out 'CwKey.ino.bin') --convert --family 0x57755a57 --base 0x08010000 --output $uf2File
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  "UF2: $uf2File"
}
