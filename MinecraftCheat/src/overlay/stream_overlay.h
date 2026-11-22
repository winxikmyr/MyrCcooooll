// src/overlay/stream_overlay.h
#pragma once
#include <windows.h>
#include <atomic>

class StreamOverlay {
public:
    static StreamOverlay& Get() {
        static StreamOverlay instance;
        return instance;
    }

    bool Start(HWND targetGameHwnd, HINSTANCE hInst);
    void Stop();

    bool IsActive() const { return m_active.load(); }
    HWND GetHwnd() const { return m_overlayHwnd; }

private:
    StreamOverlay() = default;
    ~StreamOverlay() { Stop(); }

    static DWORD WINAPI OverlayThread(LPVOID lpParam);
    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_targetHwnd = nullptr;
    HWND m_overlayHwnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HANDLE m_threadHandle = nullptr;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_stopRequested{false};
};

