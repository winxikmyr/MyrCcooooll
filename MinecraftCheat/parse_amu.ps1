Add-Type -AssemblyName System.IO.Compression.FileSystem

$jarPath = 'E:\Minecraft\game\versions\OptiFine 1.12.2\OptiFine 1.12.2.jar'
$zip = [System.IO.Compression.ZipFile]::OpenRead($jarPath)
$entry = $zip.GetEntry('amu.class')
$stream = $entry.Open()
$bytes = New-Object byte[] $entry.Length
$total = 0
while ($total -lt $bytes.Length) {
    $read = $stream.Read($bytes, $total, $bytes.Length - $total)
    if ($read -le 0) { break }
    $total += $read
}
$stream.Dispose()
$zip.Dispose()

# Parse constant pool strings
$str = [System.Text.Encoding]::ASCII.GetString($bytes)
$matches = [regex]::Matches($str, 'Ljava/util/List;')
Write-Host "Found $($matches.Count) List occurrences"
# Let's inspect javap output if javap exists
if (Get-Command javap -ErrorAction SilentlyContinue) {
    javap -cp 'E:\Minecraft\game\versions\OptiFine 1.12.2\OptiFine 1.12.2.jar' amu
}

