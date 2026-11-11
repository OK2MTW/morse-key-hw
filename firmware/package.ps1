# Builds the CwKey release package for both Black Pill variants (build only, no flashing).
#   .\package.ps1
# Output:
#   firmware\dist\cwkey-<ver>\     versioned package (kept for every release)
#   firmware\latest\               the same files under stable names (replaced by each release),
#                                  for permanent download links from the web application
# Per MCU (F411, F401):
#   cwkey-<ver>-<mcu>-full.bin     first install over the factory ROM DFU at 0x08000000:
#                                  TinyUF2 (0x08000000) + 0xFF padding + CwKey (0x08010000)
#   cwkey-<ver>-<mcu>.uf2          update over the TinyUF2 USB drive (application at 0x08010000)
# plus manifest.json and LICENSE-tinyuf2.txt. One firmware for both sidetone parts
# (active buzzer on PB8, speaker on PA8). Spec: docs/firmware-flashing.md
$ErrorActionPreference = 'Stop'
$fw = $PSScriptRoot

$src = Get-Content (Join-Path $fw 'CwKey\CwKey.ino') -Raw
if ($src -notmatch 'CWKEY_VERSION\[\] = "CwKey (\d+\.\d+\.\d+)"') { throw 'CWKEY_VERSION not found in CwKey.ino' }
$version = $Matches[1]
$dist = Join-Path $fw "dist\cwkey-$version"
$latest = Join-Path $fw 'latest'
if (Test-Path $latest) { Get-ChildItem $latest -File | Remove-Item }   # latest = exactly one release
New-Item -ItemType Directory -Force $dist, $latest | Out-Null

$chips = [ordered]@{
  'F411' = @{ boot = 'tinyuf2-0.36.0-nowrp-stm32f411ce_blackpill.bin'; ramTop = 0x20020000; mcu = 'STM32F411CE'
              label = 'STMF411BOOT'; model = 'ST STM32F411 BlackPill'; boardId = 'STM32F411-BlackPill-rev2'
              vidpid = '239A:0069'; blId = '0xD0'; layout = '@Internal Flash  /0x08000000/04*016Kg,01*064Kg,03*128Kg' }
  'F401' = @{ boot = 'tinyuf2-0.36.0-nowrp-stm32f401_blackpill.bin'; ramTop = 0x20018000; mcu = 'STM32F401CC/CD/CE'
              label = 'BlackPill'; model = 'STM32 STM32F401CxUx'; boardId = 'BlackPill'
              vidpid = '239A:005D'; blId = '0xD1'; layout = '@Internal Flash  /0x08000000/04*016Kg,01*064Kg,0N*128Kg (N = 1 CC, 2 CD, 3 CE)' }
}
$appOffset = 0x10000

function Describe([string]$dir, [string]$name) {
  $p = Join-Path $dir $name
  [ordered]@{ file = $name; size = (Get-Item $p).Length; sha256 = (Get-FileHash $p -Algorithm SHA256).Hash.ToLower() }
}

$targets = @(); $latestTargets = @()
foreach ($mcu in $chips.Keys) {
  $c = $chips[$mcu]; $m = $mcu.ToLower()
  & (Join-Path $fw 'build.ps1') -Mcu $mcu -Uf2 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "build $mcu failed" }
  $buildDir = Join-Path $fw "CwKey\build-$m-uf2"
  $app = [IO.File]::ReadAllBytes((Join-Path $buildDir 'CwKey.ino.bin'))
  $sp = [BitConverter]::ToUInt32($app, 0); $reset = [BitConverter]::ToUInt32($app, 4)
  if ($sp -ne $c.ramTop) { throw ("{0}: stack pointer 0x{1:X8}, expected 0x{2:X8}" -f $mcu, $sp, $c.ramTop) }
  if ($reset -lt 0x08010000 -or $reset -ge 0x08060000) { throw ("{0}: reset vector 0x{1:X8} outside the application area" -f $mcu, $reset) }

  Copy-Item (Join-Path $buildDir "CwKey-$mcu.uf2") (Join-Path $dist "cwkey-$version-$m.uf2") -Force
  Copy-Item (Join-Path $buildDir "CwKey-$mcu.uf2") (Join-Path $latest "cwkey-$m.uf2") -Force

  $boot = [IO.File]::ReadAllBytes((Join-Path $fw "bootloader\$($c.boot)"))
  if ($boot.Length -gt $appOffset) { throw "bootloader $mcu larger than 64 KB" }
  $full = New-Object byte[] ($appOffset + $app.Length)
  for ($i = 0; $i -lt $full.Length; $i++) { $full[$i] = 0xFF }
  [Array]::Copy($boot, 0, $full, 0, $boot.Length)
  [Array]::Copy($app, 0, $full, $appOffset, $app.Length)
  [IO.File]::WriteAllBytes((Join-Path $dist "cwkey-$version-$m-full.bin"), $full)
  [IO.File]::WriteAllBytes((Join-Path $latest "cwkey-$m-full.bin"), $full)

  foreach ($set in @(@{ dir = $dist; pre = "cwkey-$version-$m"; latest = $false }, @{ dir = $latest; pre = "cwkey-$m"; latest = $true })) {
    $t = [ordered]@{
      mcu = $c.mcu
      full = (Describe $set.dir "$($set.pre)-full.bin") + [ordered]@{ base = '0x08000000' }
      uf2 = Describe $set.dir "$($set.pre).uf2"
      uf2_app_base = '0x08010000'
      bootloader_drive = [ordered]@{ volume_label = $c.label; info_model = $c.model; info_board_id = $c.boardId; usb_vid_pid = $c.vidpid }
      rom_dfu = [ordered]@{ usb_vid_pid = '0483:DF11'; bootloader_id = $c.blId; flash_layout = $c.layout }
    }
    if ($set.latest) { $latestTargets += $t } else { $targets += $t }
  }
}

foreach ($d in $dist, $latest) { Copy-Item (Join-Path $fw 'bootloader\LICENSE-tinyuf2.txt') $d -Force }

foreach ($set in @(@{ dir = $dist; t = $targets }, @{ dir = $latest; t = $latestTargets })) {
  $manifest = [ordered]@{
    name = 'CwKey'
    version = $version
    uf2_family_id = '0x57755a57'
    bootloader = 'TinyUF2 0.36.0-morse-key-hw-nowrp (TINYUF2_PROTECT_BOOTLOADER=0, never writes option bytes)'
    outputs = [ordered]@{
      buzzer = 'PB8: active buzzer, HIGH while the sidetone sounds'
      speaker = 'PA8: passive transducer / speaker via NPN transistor, PWM tone'
      key = 'PB9: transmitter KEY via NPN transistor'
      ptt = 'PB5: transmitter PTT via NPN transistor'
    }
    targets = $set.t
  }
  # LF line endings and no BOM, so the SHA-256 of the file is the same on every platform.
  $json = ($manifest | ConvertTo-Json -Depth 6) -replace "`r`n", "`n"
  [IO.File]::WriteAllText((Join-Path $set.dir 'manifest.json'), $json + "`n", (New-Object Text.UTF8Encoding $false))
}

"Package $version"
Get-ChildItem $dist, $latest | Select-Object Directory, Name, Length | Out-String
