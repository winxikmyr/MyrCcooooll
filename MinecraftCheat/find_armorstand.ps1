Add-Type -AssemblyName System.IO.Compression.FileSystem

$paths = @(
    'E:\Minecraft\game\versions\OptiFine 1.12.2\OptiFine 1.12.2.jar',
    'C:\Users\PC\AppData\Roaming\.minecraft\versions\1.12.2\1.12.2.jar'
)

foreach ($jarPath in $paths) {
    if (Test-Path $jarPath) {
        Write-Host "Checking: $jarPath"
        $zip = [System.IO.Compression.ZipFile]::OpenRead($jarPath)
        foreach ($entry in $zip.Entries) {
            if ($entry.Name -like '*.class' -and $entry.Length -lt 100000) {
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
                if ($str -match 'minecraft:armor_stand' -or $str -match 'entity\.ArmorStand\.name' -or $str -match 'ShowArms') {
                    Write-Host "Found ArmorStand class: $($entry.FullName)"
                }
            }
        }
        $zip.Dispose()
    }
}

