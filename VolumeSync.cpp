/**
 * @file VolumeSync.cpp
 * @brief Implementation of the volume synchronization service, COM handlers, and audio endpoint management.
 * @details Implements IMMNotificationClient, IAudioEndpointVolumeCallback, and IAudioSessionNotification
 *          to establish real-time, zero-lag volume synchronization across all active playback endpoints.
 */

#include "VolumeSync.h"
#include <algorithm>
#include <cwctype>
#include <cmath>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <thread>

// ============================================================================
// EndpointVolumeCallback Implementation
// ============================================================================

/**
 * @brief Constructs the master volume change notification client.
 * @param pService Pointer to the parent VolumeSyncService instance.
 */
EndpointVolumeCallback::EndpointVolumeCallback(VolumeSyncService* pService) :
    m_cRef(1), m_pService(pService) {
}

/**
 * @brief Destructs the volume change callback handler.
 */
EndpointVolumeCallback::~EndpointVolumeCallback() {
}

/**
 * @brief Directs COM interface query requests to appropriate interfaces.
 * @param riid Reference identifier of the requested interface.
 * @param ppvInterface Receives the pointer to the interface.
 * @return HRESULT status.
 */
STDMETHODIMP EndpointVolumeCallback::QueryInterface(REFIID riid, void** ppvInterface) {
    if (ppvInterface == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == __uuidof(IAudioEndpointVolumeCallback)) {
        *ppvInterface = static_cast<IAudioEndpointVolumeCallback*>(this);
        AddRef();
        return S_OK;
    }
    *ppvInterface = NULL;
    return E_NOINTERFACE;
}

/**
 * @brief Increments COM reference count.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) EndpointVolumeCallback::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

/**
 * @brief Decrements COM reference count and self-destructs if reference drops to 0.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) EndpointVolumeCallback::Release() {
    ULONG ulRef = InterlockedDecrement(&m_cRef);
    if (ulRef == 0) {
        delete this;
    }
    return ulRef;
}

/**
 * @brief Invoked when master volume or mute status changes on the hooked default playback device.
 * @param pNotify Pointer to the audio volume notification payload containing new levels.
 * @return HRESULT status.
 */
STDMETHODIMP EndpointVolumeCallback::OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify) {
    if (pNotify == NULL) return E_POINTER;
    m_pService->HandleVolumeChanged(pNotify->fMasterVolume, pNotify->bMuted);
    return S_OK;
}

// ============================================================================
// NotificationClient Implementation
// ============================================================================

/**
 * @brief Constructs the multimedia device notification client.
 * @param pService Pointer to the parent VolumeSyncService instance.
 */
NotificationClient::NotificationClient(VolumeSyncService* pService) :
    m_cRef(1), m_pService(pService) {
}

/**
 * @brief Destructs the device notification client.
 */
NotificationClient::~NotificationClient() {
}

/**
 * @brief Directs COM interface query requests to appropriate interfaces.
 * @param riid Reference identifier of the requested interface.
 * @param ppvInterface Receives the pointer to the interface.
 * @return HRESULT status.
 */
STDMETHODIMP NotificationClient::QueryInterface(REFIID riid, void** ppvInterface) {
    if (ppvInterface == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
        *ppvInterface = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    *ppvInterface = NULL;
    return E_NOINTERFACE;
}

/**
 * @brief Increments COM reference count.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) NotificationClient::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

/**
 * @brief Decrements COM reference count and self-destructs if reference drops to 0.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) NotificationClient::Release() {
    ULONG ulRef = InterlockedDecrement(&m_cRef);
    if (ulRef == 0) {
        delete this;
    }
    return ulRef;
}

/**
 * @brief Invoked when the system's default audio playback device changes.
 * @param flow Stream flow direction (eRender/eCapture).
 * @param role Audio endpoint role.
 * @param pwstrDefaultDeviceId Device ID of the new default playback device.
 * @return HRESULT status.
 */
STDMETHODIMP NotificationClient::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    m_pService->HandleDefaultDeviceChanged(flow, role, pwstrDefaultDeviceId);
    return S_OK;
}

// ============================================================================
// AudioSessionNotification Implementation
// ============================================================================

/**
 * @brief Constructs the session creation notification client.
 * @param pService Pointer to the parent VolumeSyncService instance.
 */
AudioSessionNotification::AudioSessionNotification(VolumeSyncService* pService) :
    m_cRef(1), m_pService(pService) {
}

/**
 * @brief Destructs the session creation notification client.
 */
AudioSessionNotification::~AudioSessionNotification() {
}

/**
 * @brief Directs COM interface query requests to appropriate interfaces.
 * @param riid Reference identifier of the requested interface.
 * @param ppvInterface Receives the pointer to the interface.
 * @return HRESULT status.
 */
STDMETHODIMP AudioSessionNotification::QueryInterface(REFIID riid, void** ppvInterface) {
    if (ppvInterface == NULL) return E_POINTER;
    if (riid == IID_IUnknown || riid == __uuidof(IAudioSessionNotification)) {
        *ppvInterface = static_cast<IAudioSessionNotification*>(this);
        AddRef();
        return S_OK;
    }
    *ppvInterface = NULL;
    return E_NOINTERFACE;
}

/**
 * @brief Increments COM reference count.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) AudioSessionNotification::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

/**
 * @brief Decrements COM reference count and self-destructs if reference drops to 0.
 * @return ULONG reference count.
 */
STDMETHODIMP_(ULONG) AudioSessionNotification::Release() {
    ULONG ulRef = InterlockedDecrement(&m_cRef);
    if (ulRef == 0) {
        delete this;
    }
    return ulRef;
}

/**
 * @brief Invoked when a new audio session is initialized on the target device.
 * @param NewSession Pointer to the newly created audio session.
 * @return HRESULT status.
 * @details Directly queries the session's ISimpleAudioVolume and aligns it to the current
 *          master volume instantly, solving delay issues when apps first play audio.
 */
STDMETHODIMP AudioSessionNotification::OnSessionCreated(IAudioSessionControl* NewSession) {
    if (NewSession == NULL) return E_POINTER;
    LogInfo(L"Audio Session Created Notification received.\n");
    if (m_pService) {
        m_pService->RegisterNewSession(NewSession);
    }
    return S_OK;
}

// ============================================================================
// VolumeSyncService Implementation
// ============================================================================

/**
 * @brief Constructs the VolumeSyncService with default empty states.
 */
VolumeSyncService::VolumeSyncService() :
    m_pEnumerator(NULL),
    m_pNotificationClient(NULL),
    m_pVolumeCallback(NULL),
    m_pCurrentDevice(NULL),
    m_pEndpointVolume(NULL),
    m_pSessionManager(NULL),
    m_pSessionNotification(NULL),
    m_isHooked(false),
    m_lastEffectiveVolume(-1.0f),
    m_isMuted(FALSE),
    m_isPaused(false),
    m_targetDeviceName(L"") {
}

/**
 * @brief Safely shuts down the service on destruction.
 */
VolumeSyncService::~VolumeSyncService() {
    Shutdown();
}

/**
 * @brief Initializes the multimedia device enumerator and registers default playback endpoint notifications.
 * @return True on success, false on failure.
 */
bool VolumeSyncService::Initialize() {
    LoadConfig();

    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL,
        CLSCTX_INPROC_SERVER,
        __uuidof(IMMDeviceEnumerator),
        (void**)&m_pEnumerator
    );
    if (FAILED(hr)) {
        LogEssential(L"Failed to create MMDeviceEnumerator. hr = 0x%08X\n", hr);
        return false;
    }
    
    m_pNotificationClient = new NotificationClient(this);
    hr = m_pEnumerator->RegisterEndpointNotificationCallback(m_pNotificationClient);
    if (FAILED(hr)) {
        LogEssential(L"Failed to register endpoint notification callback. hr = 0x%08X\n", hr);
        return false;
    }
    
    m_pVolumeCallback = new EndpointVolumeCallback(this);
    
    // Check default playback device at startup
    CheckAndConfigureDevice();
    
    return true;
}

/**
 * @brief Unhooks device control, unregisters COM callbacks, and releases allocated interfaces.
 */
void VolumeSyncService::Shutdown() {
    LogEssential(L"Shutting down VolumeSyncService...\n");
    
    std::lock_guard<std::mutex> lock(m_mutex);
    UnhookVolume();
    
    if (m_pEnumerator) {
        if (m_pNotificationClient) {
            m_pEnumerator->UnregisterEndpointNotificationCallback(m_pNotificationClient);
        }
        m_pEnumerator->Release();
        m_pEnumerator = NULL;
    }
    
    if (m_pNotificationClient) {
        m_pNotificationClient->Release();
        m_pNotificationClient = NULL;
    }
    
    if (m_pVolumeCallback) {
        m_pVolumeCallback->Release();
        m_pVolumeCallback = NULL;
    }
}

/**
 * @brief Retrieves the descriptive name of an audio device.
 * @param pDevice Pointer to the IMMDevice instance.
 * @param outName Receives the wide string containing the friendly name.
 * @return True if name was successfully retrieved, false otherwise.
 */
bool VolumeSyncService::GetDeviceFriendlyName(IMMDevice* pDevice, std::wstring& outName) {
    if (!pDevice) return false;
    IPropertyStore* pPropertyStore = NULL;
    HRESULT hr = pDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
    if (FAILED(hr)) {
        LogEssential(L"Failed to open property store. hr = 0x%08X\n", hr);
        return false;
    }
    
    PROPVARIANT varName;
    PropVariantInit(&varName);
    hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &varName);
    bool success = false;
    if (SUCCEEDED(hr)) {
        if (varName.vt == VT_LPWSTR && varName.pwszVal != NULL) {
            outName = varName.pwszVal;
            success = true;
        }
        PropVariantClear(&varName);
    } else {
        LogEssential(L"Failed to get friendly name property. hr = 0x%08X\n", hr);
    }
    
    pPropertyStore->Release();
    return success;
}

/**
 * @brief Loads custom configuration from config.ini file next to the executable.
 */
void VolumeSyncService::LoadConfig() {
    wchar_t exePath[MAX_PATH];
    wchar_t configPath[MAX_PATH] = L"";
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            swprintf_s(configPath, MAX_PATH, L"%ls\\config.ini", exePath);
        }
    }
    
    wchar_t targetName[256] = L"";
    GetPrivateProfileStringW(L"Device", L"Name", L"", targetName, 256, configPath);
    
    m_targetDeviceName = targetName;
    
    if (!m_targetDeviceName.empty()) {
        LogEssential(L"Loaded custom target device name from config: '%s'\n", m_targetDeviceName.c_str());
    } else {
        LogEssential(L"No custom target device name configured. Using default keywords ('ifi', 'zen dac', 'amr hd+').\n");
    }
}

/**
 * @brief Resolves the clean executable name from a session ID.
 */
std::wstring VolumeSyncService::GetExeNameFromSessionId(const std::wstring& sessionId) {
    size_t lastSlash = sessionId.find_last_of(L"\\");
    if (lastSlash == std::wstring::npos) {
        return sessionId; // Fallback if no backslash exists (e.g. PTR-based)
    }
    std::wstring exeName = sessionId.substr(lastSlash + 1);
    size_t percent = exeName.find(L"%");
    if (percent != std::wstring::npos) {
        exeName = exeName.substr(0, percent);
    }
    // Convert to lowercase
    std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::towlower);
    return exeName;
}

/**
 * @brief Gets the path to the baselines.ini configuration file.
 */
std::wstring VolumeSyncService::GetBaselinesConfigPath() {
    wchar_t exePath[MAX_PATH];
    wchar_t configPath[MAX_PATH] = L"";
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH)) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            swprintf_s(configPath, MAX_PATH, L"%ls\\baselines.ini", exePath);
        }
    }
    return configPath;
}

/**
 * @brief Reads a baseline volume from baselines.ini.
 */
float VolumeSyncService::GetPersistentBaseline(const std::wstring& exeName) {
    if (exeName.empty()) return 1.0f;
    std::wstring path = GetBaselinesConfigPath();
    wchar_t value[32] = L"";
    GetPrivateProfileStringW(L"Baselines", exeName.c_str(), L"1.0", value, 32, path.c_str());
    try {
        float val = std::wcstof(value, nullptr);
        if (val < 0.0f) val = 0.0f;
        if (val > 1.0f) val = 1.0f;
        return val;
    } catch (...) {
        return 1.0f;
    }
}

/**
 * @brief Writes a baseline volume to baselines.ini.
 */
void VolumeSyncService::SetPersistentBaseline(const std::wstring& exeName, float baseline) {
    if (exeName.empty()) return;
    std::wstring path = GetBaselinesConfigPath();
    wchar_t value[32];
    swprintf_s(value, L"%.4f", baseline);
    WritePrivateProfileStringW(L"Baselines", exeName.c_str(), value, path.c_str());
}

/**
 * @brief Compares a device friendly name against the target criteria.
 * @param deviceName Friendly name of the audio device.
 * @return True if the name matches config or default criteria.
 */
bool VolumeSyncService::IsTargetDevice(const std::wstring& deviceName) {
    std::wstring lowerName = deviceName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](wchar_t c) {
        return std::towlower(c);
    });
    
    // If a custom target device name is loaded from config, check for matches
    if (!m_targetDeviceName.empty()) {
        std::wstring lowerTarget = m_targetDeviceName;
        std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), [](wchar_t c) {
            return std::towlower(c);
        });
        return (lowerName.find(lowerTarget) != std::wstring::npos);
    }
    
    // Fallback to default keywords if no config.ini was found or configured
    if (lowerName.find(L"ifi") != std::wstring::npos ||
        lowerName.find(L"zen dac") != std::wstring::npos ||
        lowerName.find(L"amr hd+") != std::wstring::npos) {
        return true;
    }
    return false;
}

/**
 * @brief Periodically checks and evaluates default audio playback device configuration.
 * @details Hooks onto the device if it matches target iFi criteria, and unhooks/restores
 *          volumes if the default playback device switches away.
 */
void VolumeSyncService::CheckAndConfigureDevice() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_pEnumerator) return;
    
    IMMDevice* pDefaultDevice = NULL;
    HRESULT hr = m_pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDefaultDevice);
    if (FAILED(hr)) {
        LogEssential(L"No default audio endpoint found. hr = 0x%08X\n", hr);
        if (m_isHooked) {
            UnhookVolume();
        }
        return;
    }
    
    std::wstring friendlyName;
    bool hasFriendlyName = GetDeviceFriendlyName(pDefaultDevice, friendlyName);
    
    if (hasFriendlyName) {
        bool isTarget = IsTargetDevice(friendlyName);
        if (isTarget) {
            if (!m_isHooked) {
                LogEssential(L"Target device '%s' detected. Hooking volume controls...\n", friendlyName.c_str());
                HookVolume(pDefaultDevice);
            } else {
                // If already hooked, verify if device ID matches
                LPWSTR pwszCurrentId = NULL;
                LPWSTR pwszNewId = NULL;
                m_pCurrentDevice->GetId(&pwszCurrentId);
                pDefaultDevice->GetId(&pwszNewId);
                if (pwszCurrentId && pwszNewId && wcscmp(pwszCurrentId, pwszNewId) != 0) {
                    LogEssential(L"Default target device ID changed. Re-hooking...\n");
                    UnhookVolume();
                    HookVolume(pDefaultDevice);
                }
                if (pwszCurrentId) CoTaskMemFree(pwszCurrentId);
                if (pwszNewId) CoTaskMemFree(pwszNewId);
            }
        } else {
            if (m_isHooked) {
                LogEssential(L"Default device switched away from target device to '%s'. Unhooking and restoring volumes...\n", friendlyName.c_str());
                UnhookVolume();
            }
        }
    } else {
        LogEssential(L"Could not retrieve friendly name of default device.\n");
        if (m_isHooked) {
            UnhookVolume();
        }
    }
    
    pDefaultDevice->Release();
}

/**
 * @brief Hooks the master volume control and registers callbacks on the default target device.
 * @param pDevice Pointer to the target playback IMMDevice.
 * @return True on success, false on failure.
 */
bool VolumeSyncService::HookVolume(IMMDevice* pDevice) {
    if (!pDevice) return false;
    
    m_pCurrentDevice = pDevice;
    m_pCurrentDevice->AddRef();
    
    HRESULT hr = m_pCurrentDevice->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_INPROC_SERVER,
        NULL,
        (void**)&m_pEndpointVolume
    );
    if (FAILED(hr)) {
        LogEssential(L"Failed to activate IAudioEndpointVolume. hr = 0x%08X\n", hr);
        m_pCurrentDevice->Release();
        m_pCurrentDevice = NULL;
        return false;
    }
    
    hr = m_pEndpointVolume->RegisterControlChangeNotify(m_pVolumeCallback);
    if (FAILED(hr)) {
        LogEssential(L"Failed to register volume callback. hr = 0x%08X\n", hr);
        m_pEndpointVolume->Release();
        m_pEndpointVolume = NULL;
        m_pCurrentDevice->Release();
        m_pCurrentDevice = NULL;
        return false;
    }
    
    m_isHooked = true;
    
    // Read initial volume and mute status
    BOOL bMuted = FALSE;
    m_pEndpointVolume->GetMute(&bMuted);
    m_isMuted = bMuted;
    
    float fMasterVolume = 1.0f;
    m_pEndpointVolume->GetMasterVolumeLevelScalar(&fMasterVolume);
    m_lastEffectiveVolume = bMuted ? 0.0f : fMasterVolume;
    
    LogEssential(L"Successfully hooked volume. Initial master volume: %.2f (Muted: %s)\n", fMasterVolume, bMuted ? L"YES" : L"NO");
    
    // Activate and set up session manager & session notifications
    HRESULT hrSession = m_pCurrentDevice->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_INPROC_SERVER,
        NULL,
        (void**)&m_pSessionManager
    );
    if (SUCCEEDED(hrSession) && m_pSessionManager != NULL) {
        m_pSessionNotification = new AudioSessionNotification(this);
        hrSession = m_pSessionManager->RegisterSessionNotification(m_pSessionNotification);
        if (FAILED(hrSession)) {
            LogEssential(L"Failed to register session notification. hr = 0x%08X\n", hrSession);
        }
    } else {
        LogEssential(L"Failed to activate IAudioSessionManager2 in HookVolume. hr = 0x%08X\n", hrSession);
    }

    // Sync current volume to all sessions immediately, loading baseline volumes from baselines.ini
    SyncMasterVolumeToSessionsInternal(m_lastEffectiveVolume, true, false);
    
    return true;
}

/**
 * @brief Unregisters volume listeners, releases target interfaces, and restores session volumes globally.
 */
void VolumeSyncService::UnhookVolume() {
    if (!m_isHooked) return;
    
    LogEssential(L"Unhooking volume control...\n");
    
    if (m_pEndpointVolume) {
        m_pEndpointVolume->UnregisterControlChangeNotify(m_pVolumeCallback);
    }
    
    // Restore application volume configurations to what they were before hook
    RestoreSessionOriginalVolumes();
    
    // Unregister session notification and release session manager
    if (m_pSessionManager) {
        if (m_pSessionNotification) {
            m_pSessionManager->UnregisterSessionNotification(m_pSessionNotification);
            m_pSessionNotification->Release();
            m_pSessionNotification = NULL;
        }
        m_pSessionManager->Release();
        m_pSessionManager = NULL;
    }
    
    if (m_pEndpointVolume) {
        m_pEndpointVolume->Release();
        m_pEndpointVolume = NULL;
    }
    
    if (m_pCurrentDevice) {
        m_pCurrentDevice->Release();
        m_pCurrentDevice = NULL;
    }
    
    m_isHooked = false;
    m_lastEffectiveVolume = -1.0f;
    m_isMuted = FALSE;
}

/**
 * @brief Dispatches configuration checks to a detached thread when the default output device changes.
 * @param flow stream flow direction.
 * @param role target device role.
 * @param pwstrDefaultDeviceId Device ID.
 * @details Offloading default device check to a detached thread avoids COM reentrant deadlocks
 *          on the main audio dispatch callback thread.
 */
void VolumeSyncService::HandleDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) {
    if (flow == eRender && (role == eConsole || role == eMultimedia)) {
        LogEssential(L"Default output device changed notification received. Launching config update thread...\n");
        std::thread([this]() {
            HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
            CheckAndConfigureDevice();
            if (SUCCEEDED(hr)) {
                CoUninitialize();
            }
        }).detach();
    }
}

/**
 * @brief Invoked when the master volume level changes on the hooked default target device.
 * @param fNewVolume Clamped volume level [0.0f, 1.0f].
 * @param bMuted Mute state.
 */
void VolumeSyncService::HandleVolumeChanged(float fNewVolume, BOOL bMuted) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isHooked) return;
    
    // Clamp to valid range [0.0f, 1.0f]
    if (fNewVolume < 0.0f) fNewVolume = 0.0f;
    if (fNewVolume > 1.0f) fNewVolume = 1.0f;
    
    m_isMuted = bMuted;
    float fEffectiveVolume = bMuted ? 0.0f : fNewVolume;
    LogInfo(L"Volume Callback - Master Volume changed to: %.2f (Muted: %s)\n", fNewVolume, bMuted ? L"YES" : L"NO");
    
    SyncMasterVolumeToSessionsInternal(fEffectiveVolume, false, false);
    m_lastEffectiveVolume = fEffectiveVolume;
}

/**
 * @brief Thread-safe wrapper to synchronize all application sessions across all active endpoints.
 * @param fMasterVolume Target volume scalar [0.0, 1.0].
 * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
 * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
 */
void VolumeSyncService::SyncMasterVolumeToSessions(float fMasterVolume, bool bForceUpdateBaselines, bool bSaveToDisk) {
    // Thread-safe public entry point. Locks the service mutex to serialize 
    // COM access during volume updates.
    std::lock_guard<std::mutex> lock(m_mutex);
    SyncMasterVolumeToSessionsInternal(fMasterVolume, bForceUpdateBaselines, bSaveToDisk);
}

/**
 * @brief Internal routine to perform multi-device session volume synchronization.
 * @param fMasterVolume Target volume scalar [0.0, 1.0].
 * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
 * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
 * @note Caller must hold m_mutex.
 */
void VolumeSyncService::SyncMasterVolumeToSessionsInternal(float fMasterVolume, bool bForceUpdateBaselines, bool bSaveToDisk) {
    if (m_isPaused.load()) return;
    if (!m_pEnumerator) return;
    
    // ENUMERATE ALL ACTIVE PLAYBACK DEVICES:
    // This is the correct, universal design. When virtual mixer software (like Voicemeeter)
    // or custom Windows per-app routing is active, application audio sessions (such as 
    // Edge, WhatsApp, Spotify) are registered on separate virtual/hardware endpoints.
    // Enumerating all active render devices ensures that application volume sliders
    // are successfully tracked and scaled regardless of which audio cable they route to.
    IMMDeviceCollection* pCollection = NULL;
    HRESULT hr = m_pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) {
        LogEssential(L"Failed to enumerate active audio endpoints. hr = 0x%08X\n", hr);
        return;
    }
    
    UINT count = 0;
    pCollection->GetCount(&count);
    
    for (UINT i = 0; i < count; i++) {
        IMMDevice* pDevice = NULL;
        hr = pCollection->Item(i, &pDevice);
        if (SUCCEEDED(hr)) {
            IAudioSessionManager2* pSessionManager = NULL;
            hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, NULL, (void**)&pSessionManager);
            if (SUCCEEDED(hr) && pSessionManager) {
                SyncDeviceSessions(pSessionManager, fMasterVolume, bForceUpdateBaselines, bSaveToDisk);
                pSessionManager->Release();
            }
            pDevice->Release();
        }
    }
    
    pCollection->Release();
}

/**
 * @brief Enumerates and synchronizes sessions associated with a specific session manager.
 * @param pSessionManager Session manager of an audio endpoint.
 * @param fMasterVolume Target volume scalar [0.0, 1.0].
 * @param bForceUpdateBaselines If true, active sessions baseline volumes will be updated/overwritten from their current values.
 * @param bSaveToDisk If true, saves the updated baselines to baselines.ini.
 */
void VolumeSyncService::SyncDeviceSessions(IAudioSessionManager2* pSessionManager, float fMasterVolume, bool bForceUpdateBaselines, bool bSaveToDisk) {
    if (!pSessionManager) return;
    
    IAudioSessionEnumerator* pSessionEnumerator = NULL;
    HRESULT hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr)) {
        return;
    }
    
    int count = 0;
    pSessionEnumerator->GetCount(&count);
    
    for (int i = 0; i < count; i++) {
        IAudioSessionControl* pSessionControl = NULL;
        hr = pSessionEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr)) {
            continue;
        }
        
        IAudioSessionControl2* pSessionControl2 = NULL;
        hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
        std::wstring sessionId;
        bool hasControl2 = SUCCEEDED(hr) && pSessionControl2;
        
        if (hasControl2) {
            AudioSessionState state;
            pSessionControl2->GetState(&state);
            
            // Skip expired audio sessions to avoid operations on stale descriptors.
            if (state == AudioSessionStateExpired) {
                pSessionControl2->Release();
                pSessionControl->Release();
                continue;
            }
            GetSessionId(pSessionControl2, sessionId);
        } else {
            sessionId = GetFallbackSessionId(pSessionControl);
        }
        
        ISimpleAudioVolume* pSimpleVolume = NULL;
        hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);
        if (SUCCEEDED(hr) && pSimpleVolume) {
            float fCurrentVolume = 1.0f;
            pSimpleVolume->GetMasterVolume(&fCurrentVolume);
            
            float fBaseline = 1.0f;
            
            auto it = m_sessionVolumeCache.find(sessionId);
            bool bExists = (it != m_sessionVolumeCache.end());
            
            if (!bExists || bForceUpdateBaselines) {
                // Cache or update current volume as baseline.
                float fBaselineVal = 1.0f;
                std::wstring exeName = GetExeNameFromSessionId(sessionId);
                
                if (bSaveToDisk) {
                    // We are resuming from pause, so the current volume in Windows is the unscaled baseline.
                    // We update the cache and write it to baselines.ini.
                    fBaselineVal = fCurrentVolume;
                    SetPersistentBaseline(exeName, fBaselineVal);
                } else {
                    // We are starting up, switching device, or seeing a new session.
                    // We load the baseline from baselines.ini (defaulting to 1.0f).
                    fBaselineVal = GetPersistentBaseline(exeName);
                }
                
                m_sessionVolumeCache[sessionId] = fBaselineVal;
                fBaseline = fBaselineVal;
                LogInfo(L"Session '%ls' cached baseline volume: %.2f (Raw: %.2f)\n", 
                        sessionId.c_str(), fBaselineVal, fCurrentVolume);
            } else {
                fBaseline = it->second;
            }
            
            // Scale proportionally: target = baseline * current_master_volume
            float fTargetVolume = fBaseline * fMasterVolume;
            if (fTargetVolume < 0.0f) fTargetVolume = 0.0f;
            if (fTargetVolume > 1.0f) fTargetVolume = 1.0f;
            
            // Optimize: check difference threshold to avoid flooding Windows Audio Service (AudioSrv) IPC
            if (std::abs(fCurrentVolume - fTargetVolume) > 0.001f) {
                pSimpleVolume->SetMasterVolume(fTargetVolume, NULL);
            }
            pSimpleVolume->Release();
        }
        
        if (hasControl2) {
            pSessionControl2->Release();
        }
        pSessionControl->Release();
    }
    
    pSessionEnumerator->Release();
}

void VolumeSyncService::RestoreSessionOriginalVolumes() {
    if (!m_pEnumerator) return;
    
    LogEssential(L"Restoring all session volume levels on all active devices to their original cached volumes...\n");
    
    IMMDeviceCollection* pCollection = NULL;
    HRESULT hr = m_pEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pCollection);
    if (FAILED(hr)) return;
    
    UINT count = 0;
    pCollection->GetCount(&count);
    
    for (UINT i = 0; i < count; i++) {
        IMMDevice* pDevice = NULL;
        hr = pCollection->Item(i, &pDevice);
        if (SUCCEEDED(hr)) {
            IAudioSessionManager2* pSessionManager = NULL;
            hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, NULL, (void**)&pSessionManager);
            if (SUCCEEDED(hr) && pSessionManager) {
                RestoreDeviceSessions(pSessionManager);
                pSessionManager->Release();
            }
            pDevice->Release();
        }
    }
    
    pCollection->Release();
    // Note: Do NOT clear the cache here to maintain baselines for closed/inactive apps or across device switches.
}

void VolumeSyncService::RestoreDeviceSessions(IAudioSessionManager2* pSessionManager) {
    if (!pSessionManager) return;
    
    IAudioSessionEnumerator* pSessionEnumerator = NULL;
    HRESULT hr = pSessionManager->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr)) return;
    
    int count = 0;
    pSessionEnumerator->GetCount(&count);
    
    for (int i = 0; i < count; i++) {
        IAudioSessionControl* pSessionControl = NULL;
        hr = pSessionEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr)) continue;
        
        IAudioSessionControl2* pSessionControl2 = NULL;
        hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
        std::wstring sessionId;
        bool hasControl2 = SUCCEEDED(hr) && pSessionControl2;
        if (hasControl2) {
            GetSessionId(pSessionControl2, sessionId);
        } else {
            sessionId = GetFallbackSessionId(pSessionControl);
        }
        
        auto it = m_sessionVolumeCache.find(sessionId);
        if (it != m_sessionVolumeCache.end()) {
            float fOriginalVolume = it->second;
            ISimpleAudioVolume* pSimpleVolume = NULL;
            hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);
            if (SUCCEEDED(hr) && pSimpleVolume) {
                float fCurrentVolume = 1.0f;
                pSimpleVolume->GetMasterVolume(&fCurrentVolume);
                if (std::abs(fCurrentVolume - fOriginalVolume) > 0.001f) {
                    pSimpleVolume->SetMasterVolume(fOriginalVolume, NULL);
                }
                pSimpleVolume->Release();
            }
        }
        
        if (hasControl2) {
            pSessionControl2->Release();
        }
        pSessionControl->Release();
    }
    
    pSessionEnumerator->Release();
}

void VolumeSyncService::RegisterNewSession(IAudioSessionControl* pSessionControl) {
    if (!pSessionControl) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    IAudioSessionControl2* pSessionControl2 = NULL;
    HRESULT hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
    std::wstring sessionId;
    bool hasControl2 = SUCCEEDED(hr) && pSessionControl2;
    if (hasControl2) {
        GetSessionId(pSessionControl2, sessionId);
    } else {
        sessionId = GetFallbackSessionId(pSessionControl);
    }
    
    ISimpleAudioVolume* pSimpleVolume = NULL;
    hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleVolume);
    if (SUCCEEDED(hr) && pSimpleVolume) {
        float fCurrentVolume = 1.0f;
        pSimpleVolume->GetMasterVolume(&fCurrentVolume);
        
        // If the service is paused, just cache the baseline and return early
        if (m_isPaused.load()) {
            m_sessionVolumeCache[sessionId] = fCurrentVolume;
            LogInfo(L"New session '%ls' cached while paused. Baseline Volume: %.2f\n", sessionId.c_str(), fCurrentVolume);
            pSimpleVolume->Release();
            if (hasControl2) {
                pSessionControl2->Release();
            }
            return;
        }
        
        float fMasterVolume = m_lastEffectiveVolume.load();
        if (fMasterVolume < 0.0f) fMasterVolume = 1.0f; // Default if not hooked yet
        
        // Load the persistent baseline from baselines.ini
        std::wstring exeName = GetExeNameFromSessionId(sessionId);
        float fBaselineVal = GetPersistentBaseline(exeName);
        
        // Cache the session's starting volume level as its baseline.
        m_sessionVolumeCache[sessionId] = fBaselineVal;
        LogInfo(L"New session '%ls' cached. Initial Volume: %.2f, Loaded Baseline: %.2f\n", 
                sessionId.c_str(), fCurrentVolume, fBaselineVal);
        
        // Scale it immediately to match current master volume proportion
        float fTargetVolume = fBaselineVal * fMasterVolume;
        if (fTargetVolume < 0.0f) fTargetVolume = 0.0f;
        if (fTargetVolume > 1.0f) fTargetVolume = 1.0f;
        
        if (std::abs(fCurrentVolume - fTargetVolume) > 0.001f) {
            pSimpleVolume->SetMasterVolume(fTargetVolume, NULL);
            LogInfo(L"Immediately initialized new session volume to: %.2f\n", fTargetVolume);
        }
        pSimpleVolume->Release();
    }
    
    if (hasControl2) {
        pSessionControl2->Release();
    }
}

bool VolumeSyncService::GetSessionId(IAudioSessionControl2* pSessionControl2, std::wstring& outId) {
    if (!pSessionControl2) return false;
    LPWSTR pwszId = NULL;
    HRESULT hr = pSessionControl2->GetSessionInstanceIdentifier(&pwszId);
    if (SUCCEEDED(hr) && pwszId) {
        outId = pwszId;
        CoTaskMemFree(pwszId);
        return true;
    }
    return false;
}

std::wstring VolumeSyncService::GetFallbackSessionId(IAudioSessionControl* pSessionControl) {
    wchar_t buf[64];
    swprintf_s(buf, L"PTR_%p", pSessionControl);
    return buf;
}

void VolumeSyncService::SetPaused(bool bPaused) {
    if (m_isPaused.load() == bPaused) return;
    
    m_isPaused = bPaused;
    LogEssential(L"Service %ls\n", bPaused ? L"PAUSED" : L"RESUMED");
    
    if (bPaused) {
        // Restore all applications to their original baseline volumes on pause
        std::lock_guard<std::mutex> lock(m_mutex);
        RestoreSessionOriginalVolumes();
    } else {
        // Force baseline update from current Windows volume levels, scale them, and save to baselines.ini on resume
        SyncMasterVolumeToSessions(m_lastEffectiveVolume, true, true);
    }
}

void VolumeSyncService::Restart() {
    LogEssential(L"Restarting volume hook configuration...\n");
    LoadConfig();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        UnhookVolume();
    }
    CheckAndConfigureDevice();
}
