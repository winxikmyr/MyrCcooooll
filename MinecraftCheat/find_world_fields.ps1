Add-Type -AssemblyName System.IO.Compression.FileSystem

$jarPath = 'E:\Minecraft\game\versions\OptiFine 1.12.2\OptiFine 1.12.2.jar'
$zip = [System.IO.Compression.ZipFile]::OpenRead($jarPath)
foreach ($entry in $zip.Entries) {
    if ($entry.Name -eq 'amu.class' -or $entry.Name -eq 'bsb.class') {
        $stream = $entry.Open()
        $bytes = New-Object byte[] $entry.Length
        $total = 0
        while ($total -lt $bytes.Length) {
            $read = $stream.Read($bytes, $total, $bytes.Length - $total)
            if ($read -le 0) { break }
            $total += $read
        }
        $stream.Dispose()
        $str = [System.Text.Encoding]::ASCII.GetString($bytes)
        Write-Host "Found class: $($entry.Name), size: $($bytes.Length)"
        if ($str -match 'field_72996_f') { Write-Host 'Has field_72996_f' }
        if ($str -match 'loadedEntityList') { Write-Host 'Has loadedEntityList' }
    }
}
$zip.Dispose()

