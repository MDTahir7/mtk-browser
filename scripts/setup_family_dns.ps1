# Requires Administrator privileges
Write-Host "Configuring Cloudflare Family DNS (1.1.1.3 / 1.0.0.3)..." -ForegroundColor Cyan

$adapters = Get-NetAdapter | Where-Object { $_.Status -eq 'Up' }

foreach ($adapter in $adapters) {
    Write-Host "Updating DNS for adapter: $($adapter.Name)" -ForegroundColor Yellow
    try {
        Set-DnsClientServerAddress -InterfaceAlias $adapter.Name -ServerAddresses ("1.1.1.3", "1.0.0.3") -ErrorAction Stop
        Write-Host "Successfully updated $($adapter.Name)" -ForegroundColor Green
    } catch {
        Write-Host "Failed to update $($adapter.Name): $_" -ForegroundColor Red
        Write-Host "Tip: Run PowerShell as Administrator to change Windows DNS settings." -ForegroundColor Yellow
    }
}
