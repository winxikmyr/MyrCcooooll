$bdir = (Get-ChildItem -Directory | Where-Object { $_.Name -notmatch '^\.' -and $_.Name -notmatch 'Minecraft' })[0].FullName
$dll = Join-Path $bdir "MyrLegit.dll"
if (Test-Path -LiteralPath $dll) {
    $rand = [System.IO.Path]::GetRandomFileName()
    $old = Join-Path $bdir "old_$rand.dll"
    try { [System.IO.File]::Move($dll, $old); Write-Host "Renamed locked DLL to $old" } catch {}
}
$exe = Join-Path $bdir "Injector.exe"
if (Test-Path -LiteralPath $exe) {
    $rand = [System.IO.Path]::GetRandomFileName()
    $oldExe = Join-Path $bdir "old_$rand.exe"
    try { [System.IO.File]::Move($exe, $oldExe); Write-Host "Renamed locked EXE to $oldExe" } catch {}
}
& cmake --build MinecraftCheat/build --config Release
