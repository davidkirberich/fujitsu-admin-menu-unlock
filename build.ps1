param(
    [string]$Compiler = "x86_64-w64-mingw32-gcc",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$output = Join-Path $root "build"
$program = Join-Path $output "admin-menu-unlock.efi"
$test = Join-Path $output "test-admin-menu-unlock.exe"

Get-Command $Compiler -ErrorAction Stop | Out-Null
Get-Command $Python -ErrorAction Stop | Out-Null
foreach ($name in @("admin-menu-unlock.efi", "admin-menu-unlock.img", "admin-menu-unlock.iso", "test-admin-menu-unlock.exe")) {
    if (Test-Path -LiteralPath (Join-Path $output $name)) {
        throw "Refusing to overwrite build\$name. Preserve or move the previous build first."
    }
}

& $Python -c "import pycdlib; from pyfatfs.PyFat import PyFat; from pyfatfs.PyFatFS import PyFatFS"
if ($LASTEXITCODE -ne 0) {
    throw "ISO dependencies unavailable. Install requirements.txt with the selected Python."
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
& $Compiler -std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar `
    -o $test (Join-Path $root "test-admin-menu-unlock.c")
if ($LASTEXITCODE -ne 0) {
    throw "Test compilation failed."
}
& $test
if ($LASTEXITCODE -ne 0) {
    throw "Guarded-write tests failed."
}

& $Compiler -std=c11 -O2 -Wall -Wextra -Werror `
    -ffreestanding -fshort-wchar -mno-red-zone -fno-stack-protector `
    -fno-asynchronous-unwind-tables -fno-unwind-tables `
    -ffunction-sections -fdata-sections -nostdlib `
    "-Wl,--subsystem,10" "-Wl,--entry,efi_main" `
    "-Wl,--image-base,0x10000000" "-Wl,--gc-sections" `
    -o $program (Join-Path $root "admin-menu-unlock.c")
if ($LASTEXITCODE -ne 0) {
    throw "UEFI compilation failed."
}

& $Python (Join-Path $root "build-iso.py") $output
if ($LASTEXITCODE -ne 0) {
    throw "ISO packaging or readback verification failed."
}
Write-Host "Build ready: $output"
