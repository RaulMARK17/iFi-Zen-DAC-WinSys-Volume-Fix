# Configure-Autostart.ps1
# Script to configure the iFi Zen DAC Volume Sync Service to run at Windows Startup.

param(
    [Parameter(Mandatory=$false)]
    [switch]$Uninstall
)

$RegistryPath = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$ValueName = "ifiZenDACVolumeSync"
$ExePath = Join-Path $PSScriptRoot "build\ifi_volume_sync.exe"

if ($Uninstall) {
    if (Get-ItemProperty -Path $RegistryPath -Name $ValueName -ErrorAction SilentlyContinue) {
        Remove-ItemProperty -Path $RegistryPath -Name $ValueName
        Write-Host "Success: Autostart has been removed from the registry." -ForegroundColor Green
        
        # Also stop the running process if it is running
        $process = Get-Process -Name "ifi_volume_sync" -ErrorAction SilentlyContinue
        if ($process) {
            Stop-Process -Name "ifi_volume_sync" -Force
            Write-Host "Success: Stopped the running instance of the service." -ForegroundColor Green
        }
    } else {
        Write-Host "Autostart registry entry was not found. Nothing to remove." -ForegroundColor Yellow
    }
    return
}

# Verify the executable exists before installing
if (-not (Test-Path $ExePath)) {
    Write-Host "Error: Could not find ifi_volume_sync.exe at '$ExePath'." -ForegroundColor Red
    Write-Host "Please build the project first by running: cmake --build build" -ForegroundColor Red
    return
}

# Copy config.ini if it exists in the script directory to the build directory
$ConfigSrc = Join-Path $PSScriptRoot "config.ini"
$ConfigDest = Join-Path (Split-Path $ExePath) "config.ini"
if (Test-Path $ConfigSrc) {
    Copy-Item -Path $ConfigSrc -Destination $ConfigDest -Force
    Write-Host "Success: Copied config.ini to $ConfigDest" -ForegroundColor Green
}

# Set the registry key to execute the service at startup
try {
    Set-ItemProperty -Path $RegistryPath -Name $ValueName -Value "`"$ExePath`"" -Type String
    Write-Host "Success: The service has been configured to run at startup." -ForegroundColor Green
    Write-Host "Executable Path: $ExePath" -ForegroundColor Cyan
    
    # Launch it now in the background
    $process = Get-Process -Name "ifi_volume_sync" -ErrorAction SilentlyContinue
    if (-not $process) {
        Start-Process -FilePath $ExePath -WindowStyle Hidden
        Write-Host "Success: Launched the service in the background." -ForegroundColor Green
    } else {
        Write-Host "Note: The service is already running in the background." -ForegroundColor Yellow
    }
} catch {
    Write-Error "Failed to set registry startup entry: $_"
}
