/**
 * @file VolumeSync.h
 * @brief Declaration of the volume synchronization service, COM notification handlers, and audio session management.
 * @details This file declares classes and helpers to synchronize the master volume level of an iFi ZEN DAC
 *          device to all active application audio sessions globally across all audio render devices on Windows.
 */

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audiopolicy.h>
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <cstdio>

// By default, define INFO to enable full high-frequency volume change and session creation logging.
// Comment out or undefine INFO to compile the service in optimized mode with only essential logging.
#define INFO

/**
 * @brief Helper function that performs the actual logging to OutputDebugString and service.log.
 * @param format Wide-character format string.
 * @param ... Arguments.
 */
inline void LogDebugInternal(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    wchar_t buffer[1024];
    vswprintf_s(buffer, format, args);
    va_end(args);
    
    // Write to OutputDebugString for debug viewers
    OutputDebugStringW(buffer);
    
    // Write to service.log in the directory of the executable
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
        wchar_t* lastSlash = wcsrchr(path, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            wcscat_s(path, MAX_PATH, L"\\service.log");
            
            FILE* f = NULL;
            if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] %ls", 
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                         buffer);
                fclose(f);
            }
        }
    }
}

// LogEssential is always logged (startup, shutdown, device changes, errors)
#define LogEssential(format, ...) LogDebugInternal(format, ##__VA_ARGS__)

// LogInfo is only logged if INFO is defined (high frequency notifications, volume callbacks)
#ifdef INFO
    #define LogInfo(format, ...) LogDebugInternal(format, ##__VA_ARGS__)
#else
    #define LogInfo(format, ...) ((void)0)
#endif

// Forward declaration of the main service class
class VolumeSyncService;

/**
 * @class EndpointVolumeCallback
 * @brief COM client callback handler for master volume change events on the target device.
 * @details Implements the IAudioEndpointVolumeCallback interface to receive volume change notifications
 *          directly from the Windows audio engine.
 */
class EndpointVolumeCallback : public IAudioEndpointVolumeCallback {
private:
    LONG m_cRef;                    /**< COM reference count. */
    VolumeSyncService* m_pService;  /**< Pointer to the parent VolumeSyncService instance. */

public:
    /**
     * @brief Constructs the volume callback handler.
     * @param pService Pointer to the parent VolumeSyncService instance.
     */
    EndpointVolumeCallback(VolumeSyncService* pService);

    /**
     * @brief Destructor.
     */
    virtual ~EndpointVolumeCallback();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioEndpointVolumeCallback methods
    /**
     * @brief Triggered when the master volume or mute state changes on the default playback device.
     * @param pNotify Pointer to the volume notification payload.
     * @return HRESULT status code.
     */
    STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) override;
};

/**
 * @class NotificationClient
 * @brief COM client callback handler for multimedia device notifications.
 * @details Implements the IMMNotificationClient interface to detect when default playback
 *          endpoints switch (e.g. unplugging/plugging HDMI, USB DACs, headphones).
 */
class NotificationClient : public IMMNotificationClient {
private:
    LONG m_cRef;                    /**< COM reference count. */
    VolumeSyncService* m_pService;  /**< Pointer to the parent VolumeSyncService instance. */

public:
    /**
     * @brief Constructs the notification client.
     * @param pService Pointer to the parent VolumeSyncService instance.
     */
    NotificationClient(VolumeSyncService* pService);

    /**
     * @brief Destructor.
     */
    virtual ~NotificationClient();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMMNotificationClient methods
    STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override { return S_OK; }
    STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override { return S_OK; }
    STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override { return S_OK; }
    
    /**
     * @brief Triggered when the default audio render endpoint changes.
     * @param flow Data-flow direction (eRender or eCapture).
     * @param role Audio endpoint role (eConsole, eMultimedia, eCommunications).
     * @param pwstrDefaultDeviceId Device identifier of the new default playback device.
     * @return HRESULT status code.
     */
    STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override;
    STDMETHODIMP OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) override { return S_OK; }
};

/**
 * @class AudioSessionNotification
 * @brief COM client callback handler for newly created audio sessions.
 * @details Implements the IAudioSessionNotification interface to catch when applications
 *          open new audio streams, allowing the service to initialize their volume.
 */
class AudioSessionNotification : public IAudioSessionNotification {
private:
    LONG m_cRef;                    /**< COM reference count. */
    VolumeSyncService* m_pService;  /**< Pointer to the parent VolumeSyncService instance. */

public:
    /**
     * @brief Constructs the session notification client.
     * @param pService Pointer to the parent VolumeSyncService instance.
     */
    AudioSessionNotification(VolumeSyncService* pService);

    /**
     * @brief Destructor.
     */
    virtual ~AudioSessionNotification();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvInterface) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IAudioSessionNotification methods
    /**
     * @brief Triggered when a new audio session is created on the target playback device.
     * @param NewSession Pointer to the newly created audio session controller.
     * @return HRESULT status code.
     */
    STDMETHODIMP OnSessionCreated(IAudioSessionControl* NewSession) override;
};

/**
 * @class VolumeSyncService
 * @brief Core synchronization manager service.
 * @details Manages COM audio endpoints, master volume callbacks, default device tracking,
 *          and universal synchronization of all active audio sessions to the master volume.
 */
class VolumeSyncService {
private:
    IMMDeviceEnumerator* m_pEnumerator;                  /**< Windows MMDevice enumerator. */
    NotificationClient* m_pNotificationClient;            /**< Default device change callback client. */
    EndpointVolumeCallback* m_pVolumeCallback;            /**< Target master volume callback. */
    
    IMMDevice* m_pCurrentDevice;                         /**< Cached target IMMDevice interface. */
    IAudioEndpointVolume* m_pEndpointVolume;             /**< Target volume controller. */
    IAudioSessionManager2* m_pSessionManager;             /**< Session manager for session notifications. */
    AudioSessionNotification* m_pSessionNotification;   /**< Session creation callback client. */
    
    std::atomic<bool> m_isHooked;                        /**< Atomic flag indicating if the target volume is hooked. */
    std::atomic<float> m_lastEffectiveVolume;            /**< Atomic storage for the last known master volume level. */
    std::atomic<bool> m_isMuted;                         /**< Atomic flag indicating if target device is currently muted. */
    std::atomic<bool> m_isPaused;                        /**< Atomic flag indicating if the service execution is paused. */
    std::mutex m_mutex;                                  /**< Mutex protecting multi-threaded COM calls and state changes. */

    std::map<std::wstring, float> m_sessionVolumeCache;  /**< Cache mapping session instance IDs to original baseline volumes. */
    std::wstring m_targetDeviceName;                     /**< Cached target device keyword/name loaded from config. */

    /**
     * @brief Loads custom configuration from config.ini file.
     */
    void LoadConfig();

    /**
     * @brief Resolves the clean executable name from a session ID.
     */
    std::wstring GetExeNameFromSessionId(const std::wstring& sessionId);

    /**
     * @brief Gets the path to the baselines.ini configuration file.
     */
    std::wstring GetBaselinesConfigPath();

    /**
     * @brief Reads a baseline volume from baselines.ini.
     */
    float GetPersistentBaseline(const std::wstring& exeName);

    /**
     * @brief Writes a baseline volume to baselines.ini.
     */
    void SetPersistentBaseline(const std::wstring& exeName, float baseline);

    /**
     * @brief Retrives the user-friendly name of an audio device.
     * @param pDevice Pointer to the target IMMDevice.
     * @param outName Receives the device friendly name.
     * @return True if name was successfully retrieved, false otherwise.
     */
    bool GetDeviceFriendlyName(IMMDevice* pDevice, std::wstring& outName);

    /**
     * @brief Identifies if a given device name matches the target iFi DAC.
     * @param deviceName Friendly name of the device.
     * @return True if the name matches target criteria, false otherwise.
     */
    bool IsTargetDevice(const std::wstring& deviceName);

    /**
     * @brief Resolves a unique session ID for a session.
     * @param pSessionControl2 Pointer to the control interface.
     * @param outId Receives the resolved string ID.
     * @return True on success, false on failure.
     */
    bool GetSessionId(IAudioSessionControl2* pSessionControl2, std::wstring& outId);

    /**
     * @brief Generates a fallback pointer-based session ID.
     * @param pSessionControl Pointer to the session control.
     * @return Fallback session ID string.
     */
    std::wstring GetFallbackSessionId(IAudioSessionControl* pSessionControl);
    
public:
    /**
     * @brief Constructs the VolumeSyncService.
     */
    VolumeSyncService();

    /**
     * @brief Destructs the service and clean up allocations.
     */
    ~VolumeSyncService();

    /**
     * @brief Initializes the MMDevice enumerator and registers default device callbacks.
     * @return True if successfully initialized, false otherwise.
     */
    bool Initialize();

    /**
     * @brief Releases all COM clients, unregisters callbacks, and cleans up variables.
     */
    void Shutdown();

    /**
     * @brief Checks if the target device is currently hooked.
     * @return True if volume controls are hooked, false otherwise.
     */
    bool IsHooked() const { return m_isHooked.load(); }

    /**
     * @brief Gets the last effective volume (0.0f if muted, otherwise the master scalar).
     * @return Floating-point volume level [0.0, 1.0].
     */
    float GetLastEffectiveVolume() const { return m_lastEffectiveVolume.load(); }

    /**
     * @brief Checks if the target device is muted.
     * @return True if muted, false otherwise.
     */
    bool IsMuted() const { return m_isMuted.load(); }

    /**
     * @brief Checks if the volume synchronization service is currently paused.
     * @return True if paused, false otherwise.
     */
    bool IsPaused() const { return m_isPaused.load(); }

    /**
     * @brief Pauses or resumes the volume synchronization logic.
     * @param bPaused True to pause, false to resume.
     */
    void SetPaused(bool bPaused);

    /**
     * @brief Triggers a restart of the device hook configuration (unhooks, restores, and re-hooks).
     */
    void Restart();

    /**
     * @brief Handles system notifications when default playback device switches.
     * @param flow Audio stream direction.
     * @param role Audio endpoint role.
     * @param pwstrDefaultDeviceId Device ID of the new default playback device.
     */
    void HandleDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId);
    
    /**
     * @brief Processes target volume change notifications.
     * @param fNewVolume New volume scalar [0.0, 1.0].
     * @param bMuted Mute status.
     */
    void HandleVolumeChanged(float fNewVolume, BOOL bMuted);

    /**
     * @brief Registers and initializes a newly created audio session.
     * @param pSessionControl Pointer to the session control.
     */
    void RegisterNewSession(IAudioSessionControl* pSessionControl);

    /**
     * @brief Evaluates the current default device and establishes or tears down hooks as appropriate.
     */
    void CheckAndConfigureDevice();

    /**
     * @brief Hooks master volume change notifications on the specified device.
     * @param pDevice The default target IMMDevice.
     * @return True if successfully hooked, false otherwise.
     */
    bool HookVolume(IMMDevice* pDevice);

    /**
     * @brief Unregisters volume callbacks and restores session volumes.
     */
    void UnhookVolume();

      /**
       * @brief Thread-safe wrapper to synchronize all application sessions across all active endpoints.
       * @param fMasterVolume Target volume scalar [0.0, 1.0].
       * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
       * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
       */
      void SyncMasterVolumeToSessions(float fMasterVolume, bool bForceUpdateBaselines = false, bool bSaveToDisk = false);
  
      /**
       * @brief Internal routine to perform multi-device session volume synchronization.
       * @param fMasterVolume Target volume scalar [0.0, 1.0].
       * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
       * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
       * @note Caller must hold m_mutex.
       */
      void SyncMasterVolumeToSessionsInternal(float fMasterVolume, bool bForceUpdateBaselines = false, bool bSaveToDisk = false);
  
      /**
       * @brief Enumerates and synchronizes sessions associated with a specific session manager.
       * @param pSessionManager Session manager of an audio endpoint.
       * @param fMasterVolume Target volume scalar [0.0, 1.0].
       * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
       * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
       */
      void SyncDeviceSessions(IAudioSessionManager2* pSessionManager, float fMasterVolume, bool bForceUpdateBaselines = false, bool bSaveToDisk = false);
    
    /**
     * @brief Restores volume levels of all active audio sessions globally to 100%.
     */
    void RestoreSessionOriginalVolumes();

    /**
     * @brief Resets sessions on a specific session manager back to 100%.
     * @param pSessionManager Session manager of an audio endpoint.
     */
    void RestoreDeviceSessions(IAudioSessionManager2* pSessionManager);
};
