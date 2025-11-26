#include "pch.h"
#include "ChatWindow.h"
#include "GlobalNetwork.h"

HHOOK g_hKeyboardHook = nullptr;

#ifndef VK_OEM_2
#define VK_OEM_2 0xBF
#endif

#define WM_ACTIVATE_CHAT (WM_USER + 999)

struct ScrollGeometry
{
    float trackTop, trackH;
    float thumbY, thumbH;
    int totalLines, visibleCapacity, firstVisible;
    bool canScroll;
};

static ScrollGeometry CalculateScrollGeo(HWND hEdit, D2D1_RECT_F slotRect, float dpi)
{
    ScrollGeometry g = { 0 };

    g.trackTop = slotRect.top + 6.0f * dpi;
    float trackBottom = slotRect.bottom - 6.0f * dpi;
    g.trackH = trackBottom - g.trackTop;
    if (g.trackH < 1.0f)
        g.trackH = 1.0f;

    g.totalLines = (int)SendMessage(hEdit, EM_GETLINECOUNT, 0, 0);

    float lineHeight = 16.0f * dpi;
    g.visibleCapacity = (int)((slotRect.bottom - slotRect.top - (10.0f * dpi) * 2) / lineHeight);
    if (g.visibleCapacity < 1)
        g.visibleCapacity = 1;

    g.canScroll = (g.totalLines > g.visibleCapacity);

    if (g.canScroll)
    {
        float ratio = (float)g.visibleCapacity / (float)g.totalLines;
        g.thumbH = g.trackH * ratio;
        if (g.thumbH < 20.0f * dpi)
            g.thumbH = 20.0f * dpi;

        g.firstVisible = (int)SendMessage(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

        float scrollableRange = (float)(g.totalLines - g.visibleCapacity);
        float scrollPercent = (float)g.firstVisible / scrollableRange;
        if (scrollPercent < 0.0f)
            scrollPercent = 0.0f;
        if (scrollPercent > 1.0f)
            scrollPercent = 1.0f;

        g.thumbY = g.trackTop + (g.trackH - g.thumbH) * scrollPercent;
    }
    return g;
}

static void DebugPrint(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

ChatWindow* ChatWindow::s_instance = nullptr;
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN)
    {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        if (p->vkCode == VK_OEM_2)
        {
            if (ChatWindow::s_instance)
            {
                HWND hMyWnd = ChatWindow::s_instance->GetHWND();
                HWND hForeWnd = GetForegroundWindow();

                if (hMyWnd == hForeWnd || IsChild(hMyWnd, hForeWnd))
                {
                    return CallNextHookEx(NULL, nCode, wParam, lParam);
                }

                PostMessage(hMyWnd, WM_ACTIVATE_CHAT, 0, 0);

                return 1;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}
ChatWindow::ChatWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance),

    m_hWnd(nullptr),
    m_isDraggingInputScroll(false),
    m_dragInputStartY(0.0f),
    m_dragInputStartTopLine(0),
    m_hEdit(nullptr),
    m_oldEditProc(nullptr),
    m_titleHeight(36),
    m_inputBarHeight(105.0f),
    m_windowAlpha((BYTE)(255 * 0.90f)),
    m_caretVisible(false),
    m_lastCaretToggle(0),
    m_isDragging(false),
    m_dragStartMouseY(0.0f),
    m_dragStartScroll(0.0f),
    m_currentScroll(0.0f),
    m_targetScroll(0.0f),
    m_totalContentH(0.0f),
    m_isAutoScroll(true),
    m_closeHover(false),
    m_closeHoverProgress(0.0f),
    m_closeHoverTarget(0.0f),
    m_closePressed(false),
    m_sendHover(false),
    m_sendHoverProgress(0.0f),
    m_sendHoverTarget(0.0f),
    m_sendPressed(false),
    m_enterHandledFlag(false),
    m_pingMs(-1),
    m_lastSendTick(0)
{
    s_instance = this;
    m_hbrEditBg = CreateSolidBrush(RGB(30, 30, 30));
}

ChatWindow::~ChatWindow()
{
    if (g_hKeyboardHook)
    {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = nullptr;
    }

    if (global_net_engine)
    {
        global_net_engine->clearCallbacks();
    }

    if (m_hEdit)
    {
        RemoveWindowSubclass(m_hEdit, SubEditProc, 0);
    }

    DiscardDeviceResources();
    DeleteObject(m_hbrEditBg);
    s_instance = nullptr;
}

bool ChatWindow::Create(int x, int y, int w, int h, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = ChatWindow::StaticWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"DX11ChatWindow_Class";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    RegisterClassExW(&wc);

    m_hWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"Chat",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, w, h, nullptr, nullptr, m_hInstance, this);

    if (!m_hWnd)
        return false;

    if (!g_hKeyboardHook)
    {
        g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    }

    SetLayeredWindowAttributes(m_hWnd, 0, m_windowAlpha, LWA_ALPHA);

    if (FAILED(CreateDeviceIndependentResources()))
        return false;
    if (FAILED(CreateDeviceResources()))
        return false;

    CreateInputControl();

    if (global_net_engine)
    {

        global_net_engine->setUiCallback([this](std::string msg)
            {

                if (!s_instance || !IsWindow(m_hWnd)) return;

                std::wstring wmsg = StringToWString(msg);
                std::wstring* pPayload = new std::wstring(wmsg);
                PostMessageW(m_hWnd, WM_USER + 1, 0, (LPARAM)pPayload); });

        global_net_engine->setPingCallback([this](std::string ip, long long ms)
            {
                if (!s_instance || !IsWindow(m_hWnd)) return;
                PostMessageW(m_hWnd, WM_USER + 4, (WPARAM)ms, 0); });

        SetTimer(m_hWnd, 2, 3000, nullptr);
    }

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    return true;
}
void ChatWindow::CreateInputControl()
{
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    if (w <= 0)
        w = 500;
    if (h <= 0)
        h = 700;

    int inputH = (int)m_inputBarHeight;
    int editH = inputH - 20;
    int editY = h - inputH + 10;

    m_hEdit = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        15, editY, w - 30, editH,
        m_hWnd, (HMENU)200, m_hInstance, nullptr);

    if (m_hEdit)
    {

        SetWindowSubclass(m_hEdit, SubEditProc, 0, (DWORD_PTR)this);

        int dpi = GetDpiForWindow(m_hWnd);
        int fontH = -MulDiv(10, dpi, 72);
        HFONT hFont = CreateFontW(fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessage(m_hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        SendMessage(m_hEdit, EM_LIMITTEXT, 250, 0);

        SendMessage(m_hEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"To chat click here or press / or . key");
    }
}
int ChatWindow::Run()
{
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

HRESULT ChatWindow::CreateDeviceIndependentResources()
{
    HRESULT hr = S_OK;
    if (!m_factory)
    {
        D2D1_FACTORY_OPTIONS options = {};
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, reinterpret_cast<void**>(m_factory.GetAddressOf()));
        if (FAILED(hr))
        {
            DebugPrint("D2D1CreateFactory failed: 0x%08X", hr);
            return hr;
        }
    }

    if (!m_dwFactory)
    {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(m_dwFactory.GetAddressOf()));
        if (FAILED(hr))
        {
            DebugPrint("DWriteCreateFactory failed: 0x%08X", hr);
            return hr;
        }
    }

    if (!m_textFormatMsg)
    {
        hr = m_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &m_textFormatMsg);
        if (FAILED(hr))
            DebugPrint("CreateTextFormat msg failed: 0x%08X", hr);
        else
            m_textFormatMsg->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }

    if (!m_textFormatName)
    {
        hr = m_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"", &m_textFormatName);
        if (FAILED(hr))
            DebugPrint("CreateTextFormat name failed: 0x%08X", hr);
    }

    if (!m_textFormatTitle)
    {
        hr = m_dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &m_textFormatTitle);
        if (FAILED(hr))
            DebugPrint("CreateTextFormat title failed: 0x%08X", hr);
        else
            m_textFormatTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    return S_OK;
}

HRESULT ChatWindow::CreateDeviceResources()
{
    HRESULT hr = S_OK;

    if (m_d3dDevice)
        return S_OK;

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        creationFlags,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &obtained,
        context.GetAddressOf());

    if (FAILED(hr))
    {
        DebugPrint("D3D11CreateDevice failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr))
    {
        DebugPrint("Query IDXGIDevice failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr))
    {
        DebugPrint("GetAdapter failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<IDXGIFactory2> dxgiFactory2;
    {
        ComPtr<IDXGIFactory1> dxgiFactory1;
        hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(dxgiFactory1.GetAddressOf()));
        if (SUCCEEDED(hr) && dxgiFactory1)
        {
            hr = dxgiFactory1.As(&dxgiFactory2);
        }
        else
        {
            hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(dxgiFactory1.GetAddressOf()));
            if (SUCCEEDED(hr))
            {
                hr = dxgiFactory1.As(&dxgiFactory2);
            }
        }
    }

    DXGI_SWAP_CHAIN_DESC1 sd1 = {};
    sd1.Width = 0;
    sd1.Height = 0;
    sd1.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd1.Stereo = FALSE;
    sd1.SampleDesc.Count = 1;
    sd1.SampleDesc.Quality = 0;
    sd1.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd1.BufferCount = 2;
    sd1.Scaling = DXGI_SCALING_NONE;
    sd1.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd1.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;

    if (dxgiFactory2)
    {
        hr = dxgiFactory2->CreateSwapChainForHwnd(device.Get(), m_hWnd, &sd1, nullptr, nullptr, swapChain1.GetAddressOf());
        if (FAILED(hr))
        {
            DebugPrint("CreateSwapChainForHwnd failed (flip) : 0x%08X", hr);
        }
    }
    else
    {
        DebugPrint("IDXGIFactory2 not available; will try legacy CreateSwapChain.");
        hr = E_FAIL;
    }

    if (!swapChain1)
    {
        HRESULT hrLegacy = S_OK;
        ComPtr<IDXGIFactory> dxgiFactoryLegacy;
        hrLegacy = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(dxgiFactoryLegacy.GetAddressOf()));
        if (FAILED(hrLegacy))
        {
            DebugPrint("CreateDXGIFactory failed: 0x%08X", hrLegacy);
            return (FAILED(hr) ? hr : hrLegacy);
        }

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferDesc.Width = 0;
        sd.BufferDesc.Height = 0;
        sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 1;
        sd.OutputWindow = m_hWnd;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = 0;

        ComPtr<IDXGISwapChain> swapChainLegacy;
        hrLegacy = dxgiFactoryLegacy->CreateSwapChain(device.Get(), &sd, swapChainLegacy.GetAddressOf());
        if (FAILED(hrLegacy))
        {
            DebugPrint("Legacy CreateSwapChain failed: 0x%08X", hrLegacy);
            return hrLegacy;
        }

        hrLegacy = swapChainLegacy.As(&swapChain1);
        if (FAILED(hrLegacy))
        {
            DebugPrint("Query IDXGISwapChain1 from legacy swapChain failed: 0x%08X", hrLegacy);
            return hrLegacy;
        }
    }

    if (!swapChain1)
    {
        DebugPrint("Swap chain creation failed, no swap chain available.");
        return E_FAIL;
    }

    ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = swapChain1->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(dxgiBackBuffer.GetAddressOf()));
    if (FAILED(hr))
    {
        DebugPrint("GetBuffer(0) failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<IDXGIDevice> dxgiDeviceForD2D;
    hr = device.As(&dxgiDeviceForD2D);
    if (FAILED(hr))
    {
        DebugPrint("Device.As<IDXGIDevice> failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<ID2D1Device> d2dDevice;
    hr = m_factory->CreateDevice(dxgiDeviceForD2D.Get(), &d2dDevice);
    if (FAILED(hr))
    {
        DebugPrint("CreateDevice (D2D) failed: 0x%08X", hr);
        return hr;
    }

    ComPtr<ID2D1DeviceContext> d2dContext;
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
    if (FAILED(hr))
    {
        DebugPrint("CreateDeviceContext failed: 0x%08X", hr);
        return hr;
    }

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);

    ComPtr<ID2D1Bitmap1> targetBitmap;
    hr = d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &props, &targetBitmap);
    if (FAILED(hr))
    {
        DebugPrint("CreateBitmapFromDxgiSurface failed: 0x%08X", hr);
        return hr;
    }

    m_d3dDevice = device;
    m_d3dContext = context;
    m_swapChain = swapChain1;
    m_d2dDevice = d2dDevice;
    m_d2dContext = d2dContext;
    m_d2dTargetBitmap = targetBitmap;

    m_d2dContext->CreateSolidColorBrush(COLOR_BG, &m_brushBg);
    m_d2dContext->CreateSolidColorBrush(COLOR_BUBBLE_ME, &m_brushBubbleMe);
    m_d2dContext->CreateSolidColorBrush(COLOR_BUBBLE_SYS, &m_brushBubbleSys);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &m_brushTextWhite);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.7f), &m_brushTextName);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.2f, 0.2f, 0.2f), &m_brushScrollTrack);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.4f, 0.4f, 0.4f), &m_brushScrollThumb);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.06f, 0.06f, 0.95f), &m_brushTitleBar);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.2f, 0.2f), &m_brushClose);

    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());

    return S_OK;
}

void ChatWindow::EnsureDeviceResources()
{
    HRESULT hr = CreateDeviceResources();
    if (FAILED(hr))
    {
        DebugPrint("EnsureDeviceResources failed: 0x%08X", hr);
        DiscardDeviceResources();
        return;
    }
    else
    {
        if (m_d2dContext && m_d2dTargetBitmap)
        {
            m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());
        }
    }
}

void ChatWindow::DiscardDeviceResources()
{
    if (m_d2dContext)
    {
        m_d2dContext->SetTarget(nullptr);
    }
    m_d2dTargetBitmap.Reset();
    m_brushBg.Reset();
    m_brushBubbleMe.Reset();
    m_brushBubbleSys.Reset();
    m_brushTextWhite.Reset();
    m_brushTextName.Reset();
    m_brushScrollTrack.Reset();
    m_brushScrollThumb.Reset();
    m_brushTitleBar.Reset();
    m_brushClose.Reset();
    m_d2dContext.Reset();
    m_d2dDevice.Reset();
    if (m_swapChain)
    {
        m_swapChain.Reset();
    }
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}
void ChatWindow::SetLocalNick(const std::wstring& nick)
{
    m_localNick = nick;
}
LRESULT CALLBACK ChatWindow::SubEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{

    if (msg == WM_PAINT)
    {

        LRESULT res = DefSubclassProc(hWnd, msg, wParam, lParam);

        if (GetWindowTextLength(hWnd) == 0)
        {
            HDC hdc = GetDC(hWnd);
            HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            SetTextColor(hdc, RGB(100, 100, 100));
            SetBkMode(hdc, TRANSPARENT);

            RECT rc;
            GetClientRect(hWnd, &rc);
            rc.left += 2;

            DrawTextW(hdc, L"To chat click here or press / or . key", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

            SelectObject(hdc, hOldFont);
            ReleaseDC(hWnd, hdc);
        }
        return res;
    }

    if (msg == WM_MOUSEWHEEL)
    {

        ChatWindow* pThis = (ChatWindow*)dwRefData;
        if (pThis)
        {

            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int lines = (delta > 0) ? -3 : 3;
            SendMessage(hWnd, EM_LINESCROLL, 0, lines);
            InvalidateRect(pThis->m_hWnd, nullptr, FALSE);
            return 0;
        }
    }

    if (msg == WM_CHAR && wParam == VK_RETURN)
    {
        if (GetKeyState(VK_SHIFT) >= 0)
            return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        if (GetKeyState(VK_SHIFT) < 0)
            return DefSubclassProc(hWnd, msg, wParam, lParam);
        ChatWindow* pThis = (ChatWindow*)dwRefData;
        if (pThis)
            pThis->PerformSend();
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == 'A' && GetKeyState(VK_CONTROL) < 0)
    {
        SendMessage(hWnd, EM_SETSEL, 0, -1);
        return 0;
    }

    return DefSubclassProc(hWnd, msg, wParam, lParam);
}
void ChatWindow::PerformSend()
{

    DWORD now = GetTickCount();
    if (now < m_nextSendAllowedTick)
    {
        return;
    }

    int len = GetWindowTextLengthW(m_hEdit);
    if (len == 0)
        return;

    std::vector<wchar_t> buf(len + 1);
    GetWindowTextW(m_hEdit, buf.data(), len + 1);
    std::wstring text = buf.data();

    size_t first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos)
    {
        SetWindowTextW(m_hEdit, L"");
        return;
    }
    size_t last = text.find_last_not_of(L" \t\r\n");
    text = text.substr(first, (last - first + 1));

    ChatMessage msg;
    msg.name = m_localNick;
    msg.text = text;
    msg.isMine = true;
    m_messages.push_back(msg);

    std::wstring packet = m_localNick + L":" + text;

    if (global_net_engine)
    {
        global_net_engine->broadcast(WStringToString(packet));
    }

    SetWindowTextW(m_hEdit, L"");
    m_isAutoScroll = true;

    m_nextSendAllowedTick = now + 3000;

    SetTimer(m_hWnd, 3, 3000, nullptr);

    InvalidateRect(m_hWnd, nullptr, FALSE);
}

void ChatWindow::SendSyncRequest()
{
    if (!global_net_engine)
        return;
    std::wstring p = L"SYNC_REQUEST\n";
    global_net_engine->broadcast(WStringToString(p));
}

void ChatWindow::SendSyncResponse()
{
    if (!global_net_engine)
        return;
    std::wstringstream ss;
    ss << L"SYNC_RESPONSE\n";
    for (const auto& m : m_messages)
    {
        std::wstring txt = m.text;
        for (auto& c : txt)
            if (c == L'\n' || c == L'\r')
                c = L' ';
        ss << m.name << L'\t' << txt << L'\n';
    }
    global_net_engine->broadcast(WStringToString(ss.str()));
}

void ChatWindow::HandleSyncResponse(const std::wstring& payload)
{
    std::wistringstream iss(payload);
    std::wstring line;
    bool added = false;
    while (std::getline(iss, line))
    {
        if (line.empty())
            continue;
        size_t tab = line.find(L'\t');
        std::wstring name = (tab == std::wstring::npos) ? L"Peer" : line.substr(0, tab);
        std::wstring text = (tab == std::wstring::npos) ? line : line.substr(tab + 1);

        bool found = false;
        for (const auto& m : m_messages)
        {
            if (m.name == name && m.text == text)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            m_messages.push_back({ name, text, false });
            added = true;
        }
    }
    if (added)
    {
        if (m_messages.size() > (size_t)MAX_MESSAGES)
        {
            while (m_messages.size() > (size_t)MAX_MESSAGES)
                m_messages.erase(m_messages.begin());
        }
        m_isAutoScroll = true;
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK ChatWindow::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ChatWindow* pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
        pThis = (ChatWindow*)pCreate->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    }
    else
    {
        pThis = (ChatWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis)
        return pThis->WndProc(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
LRESULT ChatWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ACTIVATE_CHAT:
    {

        if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
        else ShowWindow(hWnd, SW_SHOW);

        DWORD currentThreadId = GetCurrentThreadId();
        DWORD foregroundThreadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);

        if (currentThreadId != foregroundThreadId)
        {
            AttachThreadInput(foregroundThreadId, currentThreadId, TRUE);

            SetForegroundWindow(hWnd);
            SetFocus(hWnd);

            AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);
        }
        else
        {
            SetForegroundWindow(hWnd);
        }

        if (IsWindow(m_hEdit))
        {
            SetFocus(m_hEdit);

            int len = GetWindowTextLength(m_hEdit);
            SendMessage(m_hEdit, EM_SETSEL, len, len);
        }

        return 0;
    }
    case WM_COMMAND:
    {

        if ((HWND)lParam == m_hEdit)
        {
            int code = HIWORD(wParam);

            if (code == EN_VSCROLL || code == EN_CHANGE)
            {
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        break;
    }
    case WM_HOTKEY:
    {
        if ((int)wParam == 1)
        {
            if (IsWindow(m_hEdit))
            {
                SetForegroundWindow(m_hWnd);
                SetFocus(m_hEdit);

                SendMessageW(m_hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"/");
            }
            return 0;
        }
        break;
    }

    case WM_USER + 1:
    {
        std::wstring* raw = (std::wstring*)lParam;
        if (raw)
        {
            std::wstring s = *raw;
            delete raw;

            if (s.find(L"SYS") == 0 || s.find(L"SYNC") == 0)
                return 0;

            size_t delimPos = s.find(L':');

            std::wstring senderName = L"Peer";
            std::wstring msgContent = s;

            if (delimPos != std::wstring::npos)
            {
                senderName = s.substr(0, delimPos);
                msgContent = s.substr(delimPos + 1);
            }

            ChatMessage msg;
            msg.name = senderName;
            msg.text = msgContent;
            msg.isMine = false;

            m_messages.push_back(msg);

            if (m_messages.size() > 1000)
            {
                m_messages.erase(m_messages.begin());
            }

            m_isAutoScroll = true;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_USER + 2:
    {
        SendSyncRequest();
        return 0;
    }
    case WM_USER + 3:
    {
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_USER + 4:
    {
        int ms = (int)wParam;
        m_pingMs = ms;
        InvalidateRect(m_hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_CHAR:
    {

        if ((wchar_t)wParam == L'/')
        {
            if (IsWindow(m_hEdit))
            {
                SetFocus(m_hEdit);
                SendMessageW(m_hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"/");
                return 0;
            }
        }
        break;
    }

    case WM_CREATE:
        return 0;

    case WM_SIZE:
    {
        UINT w = LOWORD(lParam);
        UINT h = HIWORD(lParam);
        float dpi = GetDpiForWindow(hWnd) / 96.0f;

        float slotMargin = 15.0f * dpi;
        float sendBtnW = 46.0f * dpi;

        int slotLeft = (int)(slotMargin);
        int slotRight = (int)(w - slotMargin - (sendBtnW + 8.0f * dpi));

        SetWindowPos(m_hEdit, nullptr, slotLeft, h - (int)(m_inputBarHeight * dpi) + (int)(slotMargin), slotRight - slotLeft, (int)(m_inputBarHeight * dpi) - (int)(slotMargin * 2), SWP_NOZORDER);

        if (m_swapChain)
        {

            DiscardDeviceResources();

            EnsureDeviceResources();
        }

        int titleHpx = (int)(m_titleHeight * dpi);
        int closeSize = (int)((titleHpx - 8.0f * dpi));
        int closeRight = w - (int)(8.0f * dpi);
        int closeLeft = closeRight - closeSize;
        int closeTop = (titleHpx - closeSize) / 2;
        int closeBottom = closeTop + closeSize;
        m_closeRect.left = closeLeft;
        m_closeRect.top = closeTop;
        m_closeRect.right = closeRight;
        m_closeRect.bottom = closeBottom;

        {
            int radius = (int)(12.0f * dpi);
            HRGN r = CreateRoundRectRgn(0, 0, w + 1, h + 1, radius, radius);
            SetWindowRgn(hWnd, r, TRUE);
        }

        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);

        if (PtInRectF(m_slotRectClient, pt.x, pt.y))
        {
            float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam);

            int scrollLines = (delta > 0) ? -3 : 3;

            SendMessage(m_hEdit, EM_LINESCROLL, 0, scrollLines);

            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        float delta = (float)GET_WHEEL_DELTA_WPARAM(wParam);
        m_targetScroll -= delta;
        m_isAutoScroll = false;
        SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        DWORD now = GetTickCount();
        bool sendDisabled = (now < m_nextSendAllowedTick);

        if (PtInRectI(m_closeRect, x, y))
        {
            m_closePressed = true;
            SetCapture(hWnd);
            m_closeHoverTarget = 1.0f;
            SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        if (PtInRectF(m_sendRectClient, x, y) && !sendDisabled)
        {
            m_sendPressed = true;
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        if (m_scrollVisible && PtInRectF(m_scrollRectClient, x, y))
        {
            float dpi = GetDpiForWindow(hWnd) / 96.0f;

            ScrollGeometry geo = CalculateScrollGeo(m_hEdit, m_slotRectClient, dpi);

            if (!geo.canScroll)
                return 0;

            float thumbBottom = geo.thumbY + geo.thumbH;

            if ((float)y >= geo.thumbY && (float)y <= thumbBottom)
            {

                m_isDraggingInputScroll = true;
                m_dragInputStartY = (float)y;
                m_dragInputStartTopLine = geo.firstVisible;
                SetCapture(hWnd);
            }
            else
            {

                float clickRatio = ((float)y - geo.trackTop) / geo.trackH;
                int targetLine = (int)(geo.totalLines * clickRatio);

                targetLine -= (geo.visibleCapacity / 2);

                SendMessage(m_hEdit, EM_LINESCROLL, 0, targetLine - geo.firstVisible);
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            return 0;
        }

        float dpi = GetDpiForWindow(hWnd) / 96.0f;
        RECT rc;
        GetClientRect(hWnd, &rc);
        float visibleH = (float)(rc.bottom - rc.top) - (m_inputBarHeight * dpi);

        if (m_totalContentH > visibleH)
        {
            float sbWidth = 20.0f * dpi;
            float sbRight = (float)(rc.right - rc.left);
            float sbLeft = sbRight - sbWidth;

            if (x >= sbLeft && x <= sbRight && y < visibleH)
            {

                float visibleRatio = visibleH / m_totalContentH;
                float thumbHeight = visibleH * visibleRatio;
                if (thumbHeight < 30.0f * dpi)
                    thumbHeight = 30.0f * dpi;

                float maxScroll = m_totalContentH - visibleH;
                float availableTrack = visibleH - thumbHeight;
                float scrollRatio = (maxScroll > 0) ? (m_currentScroll / maxScroll) : 0.0f;
                float thumbY = scrollRatio * availableTrack;

                if (y >= thumbY && y <= thumbY + thumbHeight)
                {
                    m_isDragging = true;
                    m_isAutoScroll = false;
                    m_dragStartMouseY = (float)y;
                    m_dragStartScroll = m_currentScroll;
                    SetCapture(hWnd);
                    return 0;
                }
            }
        }

        float titleHpx = (float)(m_titleHeight * dpi);
        if (y >= 0 && y <= titleHpx)
        {
            ReleaseCapture();
            SendMessage(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }

        return 0;
    }
    case WM_MOUSEMOVE:
    {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);

        if (m_isDraggingInputScroll)
        {
            float dpi = GetDpiForWindow(hWnd) / 96.0f;

            ScrollGeometry geo = CalculateScrollGeo(m_hEdit, m_slotRectClient, dpi);

            if (!geo.canScroll)
                return 0;

            float deltaY = (float)my - m_dragInputStartY;

            float availableTrack = geo.trackH - geo.thumbH;
            if (availableTrack < 1.0f)
                availableTrack = 1.0f;

            int maxHiddenLines = geo.totalLines - geo.visibleCapacity;

            float deltaRatio = deltaY / availableTrack;
            int deltaLines = (int)(deltaRatio * maxHiddenLines);

            int targetFirstLine = m_dragInputStartTopLine + deltaLines;

            if (targetFirstLine < 0)
                targetFirstLine = 0;
            if (targetFirstLine > maxHiddenLines)
                targetFirstLine = maxHiddenLines;

            int currentFirst = (int)SendMessage(m_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

            SendMessage(m_hEdit, EM_LINESCROLL, 0, targetFirstLine - currentFirst);

            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        bool inClose = PtInRectI(m_closeRect, mx, my);
        if (inClose && !m_closeHover)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            m_closeHover = true;
            m_closeHoverTarget = 1.0f;
            SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (!inClose && m_closeHover)
        {
            m_closeHover = false;
            m_closeHoverTarget = 0.0f;
            SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        bool inSend = PtInRectF(m_sendRectClient, mx, my);
        if (inSend && !m_sendHover)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
            TrackMouseEvent(&tme);
            m_sendHover = true;
            m_sendHoverTarget = 1.0f;
            SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        else if (!inSend && m_sendHover)
        {
            m_sendHover = false;
            m_sendHoverTarget = 0.0f;
            SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        if (m_isDragging)
        {
            float dpi = GetDpiForWindow(hWnd) / 96.0f;
            RECT rc;
            GetClientRect(hWnd, &rc);
            float visibleH = (float)(rc.bottom - rc.top) - (m_inputBarHeight * dpi);

            float visibleRatio = visibleH / m_totalContentH;
            float thumbHeight = visibleH * visibleRatio;
            if (thumbHeight < 30.0f * dpi)
                thumbHeight = 30.0f * dpi;

            float availableTrack = visibleH - thumbHeight;
            float maxScroll = m_totalContentH - visibleH;

            float deltaY = (float)my - m_dragStartMouseY;

            if (availableTrack > 0)
            {
                float scrollDelta = (deltaY / availableTrack) * maxScroll;
                m_targetScroll = m_dragStartScroll + scrollDelta;

                if (m_targetScroll < 0)
                    m_targetScroll = 0;
                if (m_targetScroll > maxScroll)
                    m_targetScroll = maxScroll;

                m_currentScroll = m_targetScroll;
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }

        return 0;
    }

    case WM_MOUSELEAVE:
    {
        m_closeHover = false;
        m_closeHoverTarget = 0.0f;
        m_sendHover = false;
        m_sendHoverTarget = 0.0f;
        SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);

        if (GetCapture() == hWnd)
            ReleaseCapture();
        m_isDraggingInputScroll = false;

        if (PtInRectI(m_closeRect, x, y))
        {
            DestroyWindow(hWnd);
            return 0;
        }

        if (m_isDragging)
            m_isDragging = false;

        if (m_sendPressed)
        {
            m_sendPressed = false;
            InvalidateRect(hWnd, nullptr, FALSE);

            if (PtInRectF(m_sendRectClient, x, y))
            {
                PerformSend();
            }
            return 0;
        }

        m_closePressed = false;
        m_closeHoverTarget = 0.0f;
        SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
        InvalidateRect(hWnd, nullptr, FALSE);

        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == TIMER_ID_ANIM)
        {
            InvalidateRect(hWnd, nullptr, FALSE);
        }

        else if (wParam == 3)
        {
            KillTimer(hWnd, 3);
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        HWND hUnder = WindowFromPoint(pt);

        if (hUnder == m_hEdit)
        {
            SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            return TRUE;
        }

        POINT clientPt = pt;
        ScreenToClient(hWnd, &clientPt);

        if (PtInRectF(m_sendRectClient, clientPt.x, clientPt.y))
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }

        if (PtInRectI(m_closeRect, clientPt.x, clientPt.y))
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }

        if (m_scrollVisible && PtInRectF(m_scrollRectClient, clientPt.x, clientPt.y))
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE;
        }

        if (PtInRectF(m_slotRectClient, clientPt.x, clientPt.y))
        {
            SetCursor(LoadCursor(nullptr, IDC_IBEAM));
            return TRUE;
        }

        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }

    case WM_ACTIVATE:
    {
        if (LOWORD(wParam) != WA_INACTIVE)
        {
            if (IsWindow(m_hEdit))
            {
                SetFocus(m_hEdit);
                SetTimer(hWnd, TIMER_ID_ANIM, 16, nullptr);
            }
        }
        return 0;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkColor(hdc, RGB(30, 30, 30));
        return (LRESULT)m_hbrEditBg;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hWnd, &ps);
        OnPaint();
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        if (PtInRectI(m_closeRect, pt.x, pt.y))
            return HTCLIENT;
        float dpi = GetDpiForWindow(hWnd) / 96.0f;
        int titleHpx = (int)(m_titleHeight * dpi);
        if (pt.y >= 0 && pt.y <= titleHpx)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:

        UnregisterHotKey(m_hWnd, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK ChatWindow::StaticEditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    ChatWindow* self = s_instance;

    if (msg == WM_PAINT)
    {

        LRESULT res = DefSubclassProc(hWnd, msg, wParam, lParam);

        if (GetWindowTextLength(hWnd) == 0 && GetFocus() != hWnd)
        {
            HDC hdc = GetDC(hWnd);

            HFONT hFont = (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0);
            HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

            SetTextColor(hdc, RGB(128, 128, 128));
            SetBkMode(hdc, TRANSPARENT);

            RECT rc;
            GetClientRect(hWnd, &rc);
            rc.left += 2;
            DrawTextW(hdc, L"To chat click here or press / or . key", -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);

            SelectObject(hdc, hOldFont);
            ReleaseDC(hWnd, hdc);
        }
        return res;
    }

    return CallWindowProc(self->m_oldEditProc, hWnd, msg, wParam, lParam);
}

LRESULT ChatWindow::EditSubclassProcImpl(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SETFOCUS:
        SetTimer(m_hWnd, TIMER_ID_ANIM, 16, nullptr);
        m_lastCaretToggle = GetTickCount();
        m_caretVisible = true;
        break;
    case WM_KILLFOCUS:
        m_caretVisible = false;
        break;
    }

    if (msg == WM_KEYDOWN)
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl)
        {
            switch (wParam)
            {
            case 'A':
                SendMessageW(hWnd, EM_SETSEL, 0, -1);
                return 0;
            case 'C':
                SendMessageW(hWnd, WM_COPY, 0, 0);
                return 0;
            case 'X':
                SendMessageW(hWnd, WM_CUT, 0, 0);
                return 0;
            case 'V':
                SendMessageW(hWnd, WM_PASTE, 0, 0);
                return 0;
            case 'Z':
                SendMessageW(hWnd, EM_UNDO, 0, 0);
                return 0;
            default:
                break;
            }
        }
    }

    if (msg == WM_CHAR)
    {
        if (wParam == L'\r' || wParam == L'\n')
        {
            bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!shiftDown)
            {
                if (m_enterHandledFlag)
                {
                    m_enterHandledFlag = false;
                    return 0;
                }
                return 0;
            }
        }

        m_lastCaretToggle = GetTickCount();
        m_caretVisible = true;
        SendMessageW(hWnd, EM_SCROLLCARET, 0, 0);
    }

    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        bool shiftDown = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool wasRepeat = (lParam & (1 << 30)) != 0;
        if (shiftDown)
        {
            return CallWindowProcW(m_oldEditProc, hWnd, msg, wParam, lParam);
        }
        if (wasRepeat)
        {
            return 0;
        }

        int len = GetWindowTextLengthW(hWnd);
        if (len == 0)
        {
            return 0;
        }
        std::wstring text;
        text.resize(static_cast<std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t>>::size_type>(len) + 1);
        GetWindowTextW(hWnd, &text[0], len + 1);
        text.resize(len);
        std::wstring trimmed = trim_ws(text);
        if (trimmed.empty())
        {
            return 0;
        }

        PerformSend();

        m_enterHandledFlag = true;

        m_lastCaretToggle = GetTickCount();
        m_caretVisible = true;

        return 0;
    }

    return CallWindowProcW(m_oldEditProc, hWnd, msg, wParam, lParam);
}

void ChatWindow::OnPaint()
{
    EnsureDeviceResources();
    if (!m_d2dContext)
        return;

    float dpi = GetDpiForWindow(m_hWnd) / 96.0f;
    D2D1_SIZE_F size = m_d2dContext->GetSize();
    float width = size.width;
    float height = size.height;

    float slotMargin = 10.0f * dpi;
    float sendBtnSize = 40.0f * dpi;
    float btnGap = 8.0f * dpi;
    const float inputPadding = 10.0f * dpi;

    D2D1::ColorF colorInputSlot(0.12f, 0.12f, 0.12f, 1.0f);

    D2D1::ColorF colorBtnPurple(0.27f, 0.11f, 0.45f, 1.0f);

    int len = GetWindowTextLengthW(m_hEdit);

    HDC hdc = GetDC(m_hEdit);
    HFONT hFont = (HFONT)SendMessage(m_hEdit, WM_GETFONT, 0, 0);
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    int lineHeight = tm.tmHeight;

    int totalLines = (int)SendMessage(m_hEdit, EM_GETLINECOUNT, 0, 0);
    int firstVisible = (int)SendMessage(m_hEdit, EM_GETFIRSTVISIBLELINE, 0, 0);

    SelectObject(hdc, hOldFont);
    ReleaseDC(m_hEdit, hdc);

    float textContentH = (float)(totalLines * lineHeight);

    const float minInputH = 20.0f * dpi + (inputPadding * 2);
    const float maxInputLimit = 140.0f * dpi;
    const float scrollBarWidth = 5.0f * dpi;

    float calculatedH = textContentH + (inputPadding * 2);
    float finalInputH = calculatedH;
    bool needScroll = false;

    if (calculatedH < minInputH)
    {
        finalInputH = minInputH;
    }
    else if (calculatedH > maxInputLimit)
    {
        finalInputH = maxInputLimit;
        needScroll = true;
    }

    m_inputBarHeight = finalInputH + (slotMargin * 2);

    ShowScrollBar(m_hEdit, SB_VERT, FALSE);

    float zoneTopY = height - m_inputBarHeight;
    float targetInputH = finalInputH;

    D2D1_RECT_F slotRect = D2D1::RectF(
        slotMargin,
        height - targetInputH - slotMargin,
        width - slotMargin - sendBtnSize - btnGap,
        height - slotMargin);
    m_slotRectClient = slotRect;

    D2D1_RECT_F sendRect = D2D1::RectF(
        slotRect.right + btnGap,
        slotRect.bottom - sendBtnSize,
        slotRect.right + btnGap + sendBtnSize,
        slotRect.bottom);
    m_sendRectClient = sendRect;

    float editWidthAvailable = (slotRect.right - slotRect.left) - (inputPadding * 2);
    if (needScroll)
    {
        editWidthAvailable -= (scrollBarWidth + 4.0f * dpi);
    }

    int editX = (int)(slotRect.left + inputPadding);
    int editY = (int)(slotRect.top + inputPadding);
    int editW = (int)editWidthAvailable;
    int editH = (int)(slotRect.bottom - slotRect.top - (inputPadding * 2));

    if (m_hEdit)
    {
        SetWindowPos(m_hEdit, nullptr, editX, editY, editW, editH, SWP_NOZORDER);
    }

    if (std::abs(m_targetScroll - m_currentScroll) > 0.5f)
        m_currentScroll += (m_targetScroll - m_currentScroll) * SCROLL_SPEED_FACTOR;
    else
        m_currentScroll = m_targetScroll;

    DWORD now = GetTickCount();
    if (now - m_lastCaretToggle >= CARET_BLINK_MS)
    {
        m_caretVisible = !m_caretVisible;
        m_lastCaretToggle = now;
    }

    m_closeHoverProgress += (m_closeHoverTarget - m_closeHoverProgress) * HOVER_ANIM_SPEED;
    m_sendHoverProgress += (m_sendHoverTarget - m_sendHoverProgress) * HOVER_ANIM_SPEED;

    m_d2dContext->BeginDraw();
    m_d2dContext->Clear(COLOR_BG);

    float titleH = m_titleHeight * dpi;

    m_d2dContext->FillRectangle(D2D1::RectF(0, 0, width, titleH), m_brushTitleBar.Get());
    {
        ComPtr<ID2D1SolidColorBrush> sep;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.12f, 0.12f, 0.9f), &sep);
        m_d2dContext->DrawLine(D2D1::Point2F(0, titleH - 0.5f), D2D1::Point2F(width, titleH - 0.5f), sep.Get(), 1.0f);
    }

    std::wstring titleText = L"Rbx Chat DX11";
    if (m_pingMs >= 0)
        titleText += L" • " + std::to_wstring(m_pingMs) + L" ms";
    m_d2dContext->DrawTextW(titleText.c_str(), (UINT32)titleText.size(), m_textFormatTitle.Get(),
        D2D1::RectF(12.0f * dpi, 0, width - 80.0f * dpi, titleH), m_brushTextWhite.Get());

    float closeSize = titleH - 8.0f * dpi;
    float closeRight = width - 8.0f * dpi;
    float closeLeft = closeRight - closeSize;
    float closeTop = (titleH - closeSize) / 2.0f;
    m_closeRect = { (LONG)closeLeft, (LONG)closeTop, (LONG)closeRight, (LONG)(closeTop + closeSize) };

    D2D1_ROUNDED_RECT closeR = D2D1::RoundedRect(D2D1::RectF(closeLeft, closeTop, closeRight, closeTop + closeSize), 6.0f, 6.0f);
    ComPtr<ID2D1SolidColorBrush> closeBrush;
    float r = (0.9f + (1.0f - 0.9f) * m_closeHoverProgress) * (m_closePressed ? 0.85f : 1.0f);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(r, 0.2f, 0.2f), &closeBrush);
    m_d2dContext->FillRoundedRectangle(closeR, closeBrush.Get());

    {
        float pad = closeSize * 0.3f;
        ComPtr<ID2D1SolidColorBrush> xbrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.95f), &xbrush);
        m_d2dContext->DrawLine(D2D1::Point2F(closeLeft + pad, closeTop + pad), D2D1::Point2F(closeRight - pad, closeTop + closeSize - pad), xbrush.Get(), 2.0f);
        m_d2dContext->DrawLine(D2D1::Point2F(closeLeft + pad, closeTop + closeSize - pad), D2D1::Point2F(closeRight - pad, closeTop + pad), xbrush.Get(), 2.0f);
    }

    float visibleChatH = zoneTopY;
    float totalH = 20.0f * dpi;
    std::vector<ComPtr<IDWriteTextLayout>> layoutsName, layoutsMsg;
    std::vector<D2D1_SIZE_F> bubbleSizes;
    float maxBubbleW = width * 0.70f;

    for (const auto& msg : m_messages)
    {
        ComPtr<IDWriteTextLayout> tlName, tlMsg;
        m_dwFactory->CreateTextLayout(msg.name.c_str(), (UINT32)msg.name.size(), m_textFormatName.Get(), maxBubbleW, 500.0f, &tlName);
        m_dwFactory->CreateTextLayout(msg.text.c_str(), (UINT32)msg.text.size(), m_textFormatMsg.Get(), maxBubbleW, 5000.0f, &tlMsg);

        DWRITE_TEXT_METRICS mn{}, mm{};
        tlName->GetMetrics(&mn);
        tlMsg->GetMetrics(&mm);

        float contentW = std::max(mn.width, mm.width);
        float bW = contentW + (inputPadding * 2);
        float bH = mn.height + mm.height + (inputPadding * 2) + 2.0f * dpi;
        if (bW < 60.0f * dpi)
            bW = 60.0f * dpi;

        layoutsName.push_back(tlName);
        layoutsMsg.push_back(tlMsg);
        bubbleSizes.push_back(D2D1::SizeF(bW, bH));
        totalH += bH + 10.0f * dpi;
    }
    m_totalContentH = totalH + 20.0f * dpi;

    float maxScroll = std::max(0.0f, m_totalContentH - visibleChatH);
    if (m_isAutoScroll)
        m_targetScroll = maxScroll;
    m_targetScroll = std::clamp(m_targetScroll, 0.0f, maxScroll);

    m_d2dContext->PushAxisAlignedClip(D2D1::RectF(0, titleH, width, visibleChatH), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    float curY = titleH + 20.0f * dpi - m_currentScroll;
    for (size_t i = 0; i < m_messages.size(); ++i)
    {
        float bW = bubbleSizes[i].width;
        float bH = bubbleSizes[i].height;
        if (curY + bH > titleH && curY < visibleChatH)
        {
            float left = m_messages[i].isMine ? (width - bW - 20.0f * dpi) : 20.0f * dpi;
            D2D1_RECT_F bRect = D2D1::RectF(left, curY, left + bW, curY + bH);
            ID2D1Brush* bBrush = m_messages[i].isMine ? m_brushBubbleMe.Get() : m_brushBubbleSys.Get();

            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(bRect, 12.0f, 12.0f), bBrush);

            m_d2dContext->DrawTextLayout(D2D1::Point2F(bRect.left + inputPadding, bRect.top + inputPadding),
                layoutsName[i].Get(), m_brushTextName.Get());

            DWRITE_TEXT_METRICS mn;
            layoutsName[i]->GetMetrics(&mn);
            m_d2dContext->DrawTextLayout(D2D1::Point2F(bRect.left + inputPadding, bRect.top + inputPadding + mn.height),
                layoutsMsg[i].Get(), m_brushTextWhite.Get());
        }
        curY += bH + 10.0f * dpi;
    }
    m_d2dContext->PopAxisAlignedClip();

    m_d2dContext->FillRectangle(D2D1::RectF(0, zoneTopY, width, height), m_brushBg.Get());

    ComPtr<ID2D1SolidColorBrush> brushSlot;
    m_d2dContext->CreateSolidColorBrush(colorInputSlot, &brushSlot);
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(slotRect, 8.0f, 8.0f), brushSlot.Get());

    ComPtr<ID2D1SolidColorBrush> borderSlot;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.25f, 0.25f), &borderSlot);
    m_d2dContext->DrawRoundedRectangle(D2D1::RoundedRect(slotRect, 8.0f, 8.0f), borderSlot.Get(), 1.0f);

    ComPtr<ID2D1SolidColorBrush> sendBrush;
    bool sendDisabled = (now < m_nextSendAllowedTick);
    if (sendDisabled)
    {
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.3f, 0.3f), &sendBrush);
    }
    else
    {

        float hoverAdd = 0.1f * m_sendHoverProgress;
        float mul = m_sendPressed ? 0.8f : 1.0f;
        m_d2dContext->CreateSolidColorBrush(
            D2D1::ColorF((colorBtnPurple.r + hoverAdd) * mul, colorBtnPurple.g * mul, colorBtnPurple.b * mul, 1.0f),
            &sendBrush);
    }
    m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(sendRect, 8.0f, 8.0f), sendBrush.Get());

    {
        float cx = (sendRect.left + sendRect.right) / 2.0f;
        float cy = (sendRect.top + sendRect.bottom) / 2.0f;
        float sz = 18.0f * dpi;
        D2D1_POINT_2F p1 = { cx - sz * 0.35f, cy + sz * 0.25f };
        D2D1_POINT_2F p2 = { cx + sz * 0.40f, cy };
        D2D1_POINT_2F p3 = { cx - sz * 0.35f, cy - sz * 0.25f };
        D2D1_POINT_2F p4 = { cx - sz * 0.15f, cy };

        ComPtr<ID2D1PathGeometry> geom;
        m_factory->CreatePathGeometry(&geom);
        ComPtr<ID2D1GeometrySink> sink;
        geom->Open(&sink);
        sink->BeginFigure(p1, D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(p2);
        sink->AddLine(p3);
        sink->AddLine(p4);
        sink->AddLine(p1);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        ComPtr<ID2D1SolidColorBrush> iconBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &iconBrush);
        m_d2dContext->FillGeometry(geom.Get(), iconBrush.Get());
    }

    if (len == 0 && GetFocus() != m_hEdit)
    {

        ComPtr<ID2D1SolidColorBrush> phBrush;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.5f, 0.5f, 0.5f), &phBrush);
        D2D1_RECT_F phRect = D2D1::RectF((float)editX, (float)editY, (float)(editX + editW), (float)(editY + editH));

        std::wstring hint = L"To chat click here or press / key";
        m_d2dContext->DrawTextW(hint.c_str(), (UINT32)hint.size(), m_textFormatMsg.Get(), phRect, phBrush.Get());
    }
    else
    {
    }

    if (m_totalContentH > visibleChatH)
    {
        float sbW = 6.0f * dpi;
        float trackH = visibleChatH - titleH;
        float thumbH = trackH * (visibleChatH / m_totalContentH);
        if (thumbH < 30.0f * dpi)
            thumbH = 30.0f * dpi;
        float ratio = (maxScroll > 0) ? (m_currentScroll / maxScroll) : 0.0f;
        float thumbY = titleH + (trackH - thumbH) * ratio;
        D2D1_RECT_F thumbRect = D2D1::RectF(width - sbW - 2.0f, thumbY, width - 2.0f, thumbY + thumbH);
        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(thumbRect, 3.0f, 3.0f), m_brushScrollThumb.Get());
    }

    ScrollGeometry geo = CalculateScrollGeo(m_hEdit, m_slotRectClient, dpi);
    m_scrollVisible = geo.canScroll;

    if (m_scrollVisible)
    {
        float trackRight = slotRect.right - 4.0f * dpi;

        m_scrollRectClient = D2D1::RectF(trackRight - 15.0f * dpi, geo.trackTop, trackRight + 2.0f * dpi, geo.trackTop + geo.trackH);

        D2D1_ROUNDED_RECT thumbRect = D2D1::RoundedRect(
            D2D1::RectF(trackRight - scrollBarWidth, geo.thumbY, trackRight, geo.thumbY + geo.thumbH),
            2.5f, 2.5f);
        m_d2dContext->FillRoundedRectangle(thumbRect, m_brushScrollThumb.Get());
    }
    else
    {
        m_scrollRectClient = { 0 };
    }

    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
    if (m_swapChain)
        m_swapChain->Present(1, 0);
}
std::wstring ChatWindow::trim_ws(const std::wstring& s)
{
    size_t a = 0;
    while (a < s.size() && iswspace(s[a]))
        ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1]))
        --b;
    return s.substr(a, b - a);
}

bool ChatWindow::PtInRectI(const RECT& r, int x, int y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
bool ChatWindow::PtInRectF(const D2D1_RECT_F& r, int x, int y)
{
    return x >= (int)r.left && x <= (int)r.right && y >= (int)r.top && y <= (int)r.bottom;
}