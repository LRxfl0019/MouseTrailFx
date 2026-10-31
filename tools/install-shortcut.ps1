# Create Desktop + Start Menu shortcuts for MouseTrailFx
$ErrorActionPreference = 'SilentlyContinue'
$exe = Join-Path $PSScriptRoot 'MouseTrailFx.exe'
if (-not (Test-Path $exe)) { Write-Output "exe not found: $exe"; exit 1 }

$ws = New-Object -ComObject WScript.Shell
$targets = @(
    [Environment]::GetFolderPath('Desktop'),
    [Environment]::GetFolderPath('Programs')
)
$made = 0
foreach ($dir in $targets) {
    if (-not $dir) { continue }
    $lnk = Join-Path $dir 'MouseTrailFx.lnk'
    $s = $ws.CreateShortcut($lnk)
    $s.TargetPath = $exe
    $s.WorkingDirectory = $PSScriptRoot
    $s.IconLocation = "$exe,0"
    $s.Description = 'Mouse trail and click effects'
    $s.Save()
    if (Test-Path $lnk) { $made++ }
}
Write-Output "shortcuts created: $made"
