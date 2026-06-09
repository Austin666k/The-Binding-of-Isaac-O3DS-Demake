$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Target = "binding_of_isaac_3ds"
$Bannertool = Join-Path $ProjectDir "cia_tools\bannertool-1.2.0\windows-x86_64\bannertool.exe"
$Makerom = Join-Path $ProjectDir "cia_tools\makerom-win\makerom.exe"
$Assets = Join-Path $ProjectDir "cia_assets"

if (-not (Test-Path $Makerom)) {
    Write-Host "makerom.exe not found. Downloading Windows build..."
    $zip = Join-Path $ProjectDir "cia_tools\makerom-v0.19.0-win_x86_64.zip"
    Invoke-WebRequest -Uri "https://github.com/3DSGuy/Project_CTR/releases/download/makerom-v0.19.0/makerom-v0.19.0-win_x86_64.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath (Join-Path $ProjectDir "cia_tools\makerom-win") -Force
}

Write-Host "Building .elf via devkitPro MSYS2..."
$bash = "C:\devkitPro\msys2\usr\bin\bash.exe"
& $bash -lc "export DEVKITPRO=/c/devkitPro && export DEVKITARM=/c/devkitPro/devkitARM && export CTRULIB=/c/devkitPro/libctru && cd /c/Users/Admin/The-Binding-of-Isaac-O3DS-Demake && make"

Write-Host "Generating banner and icon..."
& $Bannertool makebanner -i (Join-Path $Assets "banner.png") -a (Join-Path $Assets "audio.wav") -o (Join-Path $Assets "banner.bnr")
& $Bannertool makesmdh -s "Isaac 3DS" -l "Binding of Isaac 3DS" -p "homebrew" -i (Join-Path $Assets "icon.png") -o (Join-Path $Assets "icon.icn")

Write-Host "Packing CIA..."
& $Makerom -f cia `
    -o (Join-Path $ProjectDir "$Target.cia") `
    -rsf (Join-Path $ProjectDir "app.rsf") `
    -target t `
    -elf (Join-Path $ProjectDir "$Target.elf") `
    -icon (Join-Path $Assets "icon.icn") `
    -banner (Join-Path $Assets "banner.bnr") `
    -DAPP_ROMFS=(Join-Path $ProjectDir "romfs")

Get-Item (Join-Path $ProjectDir "$Target.cia") | Format-List Name, Length, LastWriteTime
Write-Host "CIA build complete."