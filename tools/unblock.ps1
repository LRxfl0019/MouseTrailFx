# 去掉「Windows 已保护你的电脑」提示：解除文件的“来自 Internet”标记（Mark-of-the-Web）
# 用法：
#   .\unblock.ps1                 # 解除本目录 exe + 下载/临时目录里的副本
#   .\unblock.ps1 -Path "C:\xx\MouseTrailFx.exe"
param([string]$Path = "")

$targets = New-Object System.Collections.Generic.List[string]
if ($Path -ne "") {
    $targets.Add($Path)
} else {
    $targets.Add((Join-Path $PSScriptRoot 'MouseTrailFx.exe'))
    foreach ($dir in @("$env:USERPROFILE\Downloads", "$env:TEMP", 'D:\下载')) {
        if (-not (Test-Path $dir)) { continue }
        Get-ChildItem $dir -Recurse -Filter 'MouseTrailFx*.exe' -ErrorAction SilentlyContinue |
            ForEach-Object { $targets.Add($_.FullName) }
    }
}

$done = 0
foreach ($t in $targets) {
    if (-not (Test-Path $t)) { continue }
    $had = [bool](Get-Item $t -Stream Zone.Identifier -ErrorAction SilentlyContinue)
    Unblock-File -Path $t -ErrorAction SilentlyContinue
    $still = [bool](Get-Item $t -Stream Zone.Identifier -ErrorAction SilentlyContinue)
    Write-Output ("{0}  motw_before={1} motw_after={2}" -f $t, $had, $still)
    if (-not $still) { $done++ }
}
Write-Output "unblocked: $done"
