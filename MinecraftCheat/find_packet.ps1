Add-Type -AssemblyName System.IO.Compression.FileSystem
$jar = [System.IO.Compression.ZipFile]::OpenRead('E:\Minecraft\game\versions\OptiFine 1.12.2\OptiFine 1.12.2.jar')
foreach ($entry in $jar.Entries) {
    if ($entry.Name -like '*.class') {
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
        if ($str -match 'ofKeyBindZoom') {
            Write-Host "Found ofKeyBindZoom in: $($entry.Name)"
        }
    }
}
$jar.Dispose()
