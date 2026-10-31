# 需要管理员权限运行（HKCU\...\Policies 键的 ACL 只有 SYSTEM/Administrators 可写）
$k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Policies\Attachments'
New-Item -Path $k -Force | Out-Null
Set-ItemProperty -Path $k -Name SaveZoneInformation -Type DWord -Value 1
Set-ItemProperty -Path $k -Name LowRiskFileTypes -Type String -Value '.exe;.msi;.bat;.cmd;.zip;.rar;.7z'
Write-Host "已写入附件管理器策略，重启资源管理器后生效。" -ForegroundColor Green
