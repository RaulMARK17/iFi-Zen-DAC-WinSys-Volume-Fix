/**
 * @file main.cpp
 * @brief Application entry point and main message loop for the background volume synchronization service.
 * @details Implements a lightweight, hidden Win32 application that registers a global mutex
 *          to prevent duplicate instances, initializes COM, starts a self-healing timer,
 *          and runs a background message pump to handle notifications.
 */

#include <windows.h>
#include <shellapi.h>
#include "VolumeSync.h"

/**
 * @brief Global pointer referencing the active VolumeSyncService instance.
 * @details Required by the Win32 TimerCallback, which runs on a system thread pool
 *          and lacks access to a local class context.
 */
VolumeSyncService* g_pService = NULL;

/**
 * @brief Timer callback routine called periodically (every 2 seconds).
 * @param hwnd Handle to the window associated with the timer (NULL in this service).
 * @param uMsg WM_TIMER message.
 * @param idEvent Timer identifier.
 * @param dwTime System time in milliseconds.
 * @details Runs self-healing checks when the iFi DAC is disconnected (to re-hook)
 *          and executes global volume updates periodically when hooked to catch
 *          newly created sessions on virtual cables or other devices.
 */
VOID CALLBACK TimerCallback(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_pService) {
        if (g_pService->IsPaused()) {
            return;
        }
        if (!g_pService->IsHooked()) {
            g_pService->CheckAndConfigureDevice();
        } else {
            float lastVol = g_pService->GetLastEffectiveVolume();
            if (lastVol >= 0.0f) {
                g_pService->SyncMasterVolumeToSessions(lastVol);
            }
        }
    }
}

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_PAUSE 2001
#define ID_TRAY_RESTART 2002
#define ID_TRAY_EXIT 2003

/**
 * @brief Window procedure for the hidden helper window.
 * @details Processes right-click mouse events on the notification tray icon
 *          to render a dynamic context menu, and dispatches menu commands.
 */
LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP) {
                POINT curPoint;
                GetCursorPos(&curPoint);
                
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    bool isPaused = g_pService ? g_pService->IsPaused() : false;
                    if (isPaused) {
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_PAUSE, L"Reanudar");
                    } else {
                        AppendMenuW(hMenu, MF_STRING, ID_TRAY_PAUSE, L"Pausar");
                    }
                    
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESTART, L"Reiniciar");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Salir");
                    
                    // Required by TrackPopupMenu for tray icons to handle focus/dismiss correctly
                    SetForegroundWindow(hwnd);
                    
                    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hwnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case ID_TRAY_PAUSE:
                    if (g_pService) {
                        g_pService->SetPaused(!g_pService->IsPaused());
                    }
                    break;
                case ID_TRAY_RESTART:
                    if (g_pService) {
                        g_pService->Restart();
                    }
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            break;
        }
        default:
            return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

/**
 * @brief Entry point of the Win32 GUI application subsystem.
 * @param hInstance Handle to the current instance of the application.
 * @param hPrevInstance Handle to the previous instance of the application (always NULL in Win32).
 * @param lpCmdLine Pointer to a null-terminated command line string.
 * @param nCmdShow Specifies how the window is to be shown.
 * @return Exit code of the application (0 on success, non-zero on failure).
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Ensure only one instance of the service is running at any time
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\ifiZenDACVolumeSyncServiceMutex");
    if (hMutex == NULL) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        LogEssential(L"Another instance of ifiZenDACVolumeSyncService is already running. Exiting.\n");
        CloseHandle(hMutex);
        return 0;
    }

    // Initialize COM Library for multithreaded operations
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        LogEssential(L"Failed to initialize COM. hr = 0x%08X\n", hr);
        CloseHandle(hMutex);
        return 1;
    }

    LogEssential(L"ifi Zen DAC Volume Sync Service starting...\n");

    VolumeSyncService service;
    g_pService = &service;

    // Register hidden window class
    const wchar_t CLASS_NAME[] = L"ifiZenDACVolumeSyncServiceWindow";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Create hidden window
    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"iFi Volume Sync Service",
        0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        LogEssential(L"Failed to create hidden utility window.\n");
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    // Add speaker icon to system tray
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    
    // Try to load speaker icon from SndVol.exe in the System32 directory
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring sndVolPath = std::wstring(sysDir) + L"\\SndVol.exe";
    nid.hIcon = ExtractIconW(hInstance, sndVolPath.c_str(), 0);
    if (!nid.hIcon || nid.hIcon == (HICON)1) {
        nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512)); // Fallback to IDI_APPLICATION
    }
    
    wcscpy_s(nid.szTip, L"iFi Zen DAC Volume Sync");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Set up a self-healing timer to verify and reconnect every 2 seconds.
    UINT_PTR timerId = SetTimer(hwnd, 1, 2000, TimerCallback);
    if (timerId == 0) {
        LogEssential(L"Failed to set self-healing timer.\n");
    }

    if (service.Initialize()) {
        LogEssential(L"Service initialized successfully. Running background message loop...\n");
        
        // Message loop to keep the thread alive and process COM notifications on callback threads
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        if (timerId != 0) {
            KillTimer(hwnd, timerId);
        }
        g_pService = NULL;
        
        service.Shutdown();
    } else {
        LogEssential(L"Failed to initialize VolumeSyncService.\n");
        if (timerId != 0) {
            KillTimer(hwnd, timerId);
        }
        g_pService = NULL;
    }

    // Clean up system tray icon
    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (nid.hIcon) {
        DestroyIcon(nid.hIcon);
    }

    CoUninitialize();
    
    LogEssential(L"ifi Zen DAC Volume Sync Service exiting.\n");
    CloseHandle(hMutex);
    return 0;
}
