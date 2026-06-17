# iFi Zen DAC Windows System Volume Sync

A lightweight background utility for Windows that enables system volume control for iFi Zen DACs (and similar devices) by dynamically scaling individual application volumes.

## The Problem
By design, iFi Zen DACs bypass the Windows master volume control. The audio stream is passed directly to the hardware, making the Windows master volume slider and media keys ineffective. While this is ideal for bit-perfect playback, it is highly inconvenient for everyday use when you want to quickly adjust the volume using your keyboard keys or the OS volume slider.

## The Solution
This utility runs as a lightweight background service in the system tray. When you adjust the Windows system volume:
1. It intercepts the master volume change.
2. Instead of attempting to change the master volume (which the DAC ignores), it dynamically adjusts the volume of every active application session (e.g., Spotify, Chrome, Discord).
3. It scales application volumes proportionally, preserving the relative volume balance (mix) you have manually configured between different applications.
4. It instantly scales newly opened applications to the current volume level, preventing sudden volume spikes.

---

## Features
* **Zero Latency**: Written in native C++17 using Windows Core Audio APIs (MMDeviceAPI/WASAPI).
* **Minimal Footprint**: Runs in the background as a hidden helper window with a system tray icon. No bloated UI or third-party frameworks.
* **Preserves Volume Mix**: Adjusts application volumes relative to their initial baseline settings.
* **Auto-Reconnect**: Detects when the DAC is connected or disconnected and hooks/unhooks volume listeners within 2 seconds.
* **Startup Integration**: Includes a PowerShell script to easily configure the service to run at Windows startup.

---

## Configuration
You can customize the target DAC audio device name by modifying the `config.ini` file located next to the executable.

To configure the device name:
1. Right-click the system tray icon and select **Configuración**. This will automatically open (or create) the `config.ini` file in your default text editor.
2. Under `[Device]`, set `Name` to a keyword or the full name of your DAC (e.g., `Name=iFi Zen DAC`).
3. Save the file and close the editor.
4. Right-click the tray icon and select **Reiniciar** to reload the configuration and apply the changes.

*Note: If no custom name is configured or the file is missing/empty, the utility falls back to search for default keywords (`ifi`, `zen dac`, `amr hd+`).*

---

## Screenshots

### System Tray Menu
Right-clicking the tray icon provides quick options to pause, resume, restart, or exit the service.

<p align="center">
  <img src="assets/menu.png" alt="System Tray Menu" width="300px"/>
</p>

### Service in Action
A sample view of the service running in the background and logging volume synchronization:

<p align="center">
  <img src="assets/ss.png" alt="Service running sample" width="600px"/>
</p>

---

## Installation

1. Compile the project (see [Building from Source](#building-from-source)).
2. Open **PowerShell** in the repository root directory.
3. Run the configuration script to register the service to launch at startup:
   ```powershell
   .\Configure-Autostart.ps1
   ```
   *This registers the service in the registry (`HKCU:\Software\Microsoft\Windows\CurrentVersion\Run`) and starts the process in the background.*

To uninstall and remove the startup entry:
```powershell
.\Configure-Autostart.ps1 -Uninstall
```

---

## Building from Source

To compile the executable, you need [CMake](https://cmake.org/) and the MSVC C++ compiler (Visual Studio).

1. Open a terminal in the project root folder.
2. Create and enter a build directory:
   ```cmd
   mkdir build
   cd build
   ```
3. Generate the build files:
   ```cmd
   cmake ..
   ```
4. Build the executable in Release mode:
   ```cmd
   cmake --build . --config Release
   ```
The compiled executable `ifi_volume_sync.exe` will be generated in `build/` (or `build/Release/`).

---

## Technical Details
The utility uses Windows Core Audio COM interfaces:
* `IMMDeviceEnumerator` and `IMMNotificationClient` to monitor default audio endpoint changes.
* `IAudioEndpointVolume` and `IAudioEndpointVolumeCallback` to capture master volume changes.
* `IAudioSessionManager2` and `IAudioSessionEnumerator` to discover running audio sessions.
* `IAudioSessionNotification` to automatically intercept new application sessions.

Device status re-evaluation is delegated to a detached worker thread (`std::thread`) to prevent COM reentrant deadlocks on the main audio dispatch callback thread.
