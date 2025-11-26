#pragma once

#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include "ChatMessage.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dcommon.h>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

struct ChatMsg
{
    std::wstring sender;
    std::wstring content;
    bool isMe;
    DWORD timestamp;
};

class ChatWindow
{
public:
    ChatWindow(HINSTANCE hInstance);
    ~ChatWindow();

    bool Create(int x, int y, int w, int h, int nCmdShow);
    int Run();
    void SetLocalNick(const std::wstring &nick);
    void PerformSend();
    HWND GetHWND() const { return m_hWnd; }
    static ChatWindow* s_instance;

private:
    HINSTANCE m_hInstance;
    HWND m_hWnd;
    HWND m_hEdit;
    WNDPROC m_oldEditProc;
    std::vector<ChatMsg> m_messagesInternal;

    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID2D1Factory1> m_factory;
    ComPtr<ID2D1Device> m_d2dDevice;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;
    ComPtr<IDWriteFactory> m_dwFactory;
    ComPtr<IDWriteTextFormat> m_textFormatMsg;
    ComPtr<IDWriteTextFormat> m_textFormatName;
    ComPtr<IDWriteTextFormat> m_textFormatTitle;

    ComPtr<ID2D1SolidColorBrush> m_brushBg;
    ComPtr<ID2D1SolidColorBrush> m_brushBubbleMe;
    ComPtr<ID2D1SolidColorBrush> m_brushBubbleSys;
    ComPtr<ID2D1SolidColorBrush> m_brushTextWhite;
    ComPtr<ID2D1SolidColorBrush> m_brushTextName;
    ComPtr<ID2D1SolidColorBrush> m_brushScrollTrack;
    ComPtr<ID2D1SolidColorBrush> m_brushScrollThumb;
    ComPtr<ID2D1SolidColorBrush> m_brushTitleBar;
    ComPtr<ID2D1SolidColorBrush> m_brushClose;

    HBRUSH m_hbrEditBg;

    std::vector<ChatMessage> m_messages;

    int m_titleHeight;
    RECT m_closeRect;
    BYTE m_windowAlpha;

    D2D1_RECT_F m_slotRectClient;
    D2D1_RECT_F m_sendRectClient;

    bool m_caretVisible;
    DWORD m_lastCaretToggle;

    bool m_isDragging;
    float m_dragStartMouseY;
    float m_dragStartScroll;

    float m_currentScroll;
    float m_targetScroll;
    float m_totalContentH;
    bool m_isAutoScroll;

    bool m_closeHover;
    float m_closeHoverProgress;
    float m_closeHoverTarget;
    bool m_closePressed;

    bool m_sendHover;
    float m_sendHoverProgress;
    float m_sendHoverTarget;
    bool m_sendPressed;
    bool m_enterHandledFlag;

    D2D1_RECT_F m_scrollRectClient = {0};
    bool m_scrollVisible = false;
    bool m_isDraggingInputScroll = false;
    float m_dragInputStartY = 0.0f;
    int m_dragInputStartTopLine = 0;

    float m_inputBarHeight;
    const int MAX_MESSAGES = 1000;
    const DWORD CARET_BLINK_MS = 500;
    const float SCROLL_SPEED_FACTOR = 0.15f;
    const float HOVER_ANIM_SPEED = 0.18f;
    const UINT_PTR TIMER_ID_ANIM = 1;

    const D2D1::ColorF COLOR_BG = D2D1::ColorF(0.05f, 0.05f, 0.05f);
    const D2D1::ColorF COLOR_BUBBLE_SYS = D2D1::ColorF(0.15f, 0.15f, 0.15f);
    const D2D1::ColorF COLOR_BUBBLE_ME = D2D1::ColorF(0.25f, 0.1f, 0.4f);
    const COLORREF GDI_COLOR_BG = RGB(13, 13, 13);

private:
    std::wstring m_localNick;
    std::wstring m_statusMessage;
    DWORD m_statusEndTick = 0;
    DWORD m_nextSendAllowedTick = 0;

    int m_pingMs;
    DWORD m_lastSendTick;

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void EnsureDeviceResources();
    void DiscardDeviceResources();
    void OnPaint();
    void CreateInputControl();

    void SendSyncRequest();
    void SendSyncResponse();
    void HandleSyncResponse(const std::wstring &payload);
    static LRESULT CALLBACK SubEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK StaticEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK EditSubclassProcImpl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static std::wstring trim_ws(const std::wstring &s);
    static bool PtInRectI(const RECT &r, int x, int y);
    static bool PtInRectF(const D2D1_RECT_F &r, int x, int y);
};