#include "pch.h"
#include "SetupWindow.h"

static std::wstring trim_ws_local(const std::wstring &s)
{
    size_t a = 0;
    while (a < s.size() && iswspace(s[a]))
        ++a;
    size_t b = s.size();
    while (b > a && iswspace(s[b - 1]))
        --b;
    return s.substr(a, b - a);
}

bool SetupWindow::Run(HINSTANCE hInstance, std::wstring &outNick)
{
    SetupWindow win(hInstance);
    return win.ShowModal(outNick);
}

SetupWindow::SetupWindow(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_hWnd(nullptr)
{
    m_hbrEditBg = CreateSolidBrush(RGB(46, 46, 46));
}

SetupWindow::~SetupWindow()
{
    DiscardDeviceResources();
    DeleteObject(m_hbrEditBg);
    if (IsWindow(m_hWnd))
        DestroyWindow(m_hWnd);
}

bool SetupWindow::ShowModal(std::wstring &outNick)
{
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = SetupWindow::StaticWndProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"DX11SetupWindow_Class";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    int w = 680;
    int h = 540;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    m_hWnd = CreateWindowExW(WS_EX_TOPMOST |WS_EX_APPWINDOW | WS_EX_LAYERED, wc.lpszClassName, L"P2P Setup",
                             WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                             x, y, w, h, nullptr, nullptr, m_hInstance, this);

    if (!m_hWnd)
        return false;

    SetLayeredWindowAttributes(m_hWnd, 0, (BYTE)(255 * 0.95f), LWA_ALPHA);

    CreateDeviceIndependentResources();
    CreateDeviceResources();
    CreateChildControls(0, 0, w, h);
    UpdateLayout();

    wchar_t tmp[128];
    GetWindowTextW(m_hEditLocal, tmp, 128);
    int localPort = _wtoi(tmp);
    if (localPort <= 0 || localPort > 65535)
        localPort = 9000;

    m_isRunning = true;
    MSG msg;
    while (m_isRunning && GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_isSuccess)
    {
        wchar_t buf[256];
        GetWindowTextW(m_hEditName, buf, 256);
        outNick = trim_ws_local(buf);
    }

    return m_isSuccess;
}

void SetupWindow::CreateChildControls(int x, int y, int w, int h)
{
    DWORD styleSingle = WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL;
    DWORD styleMulti = WS_CHILD | WS_VISIBLE | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL;

    m_hEditName = CreateWindowW(L"EDIT", L"", styleSingle, 0, 0, 0, 0, m_hWnd, (HMENU)101, m_hInstance, nullptr);
    m_hEditIPs = CreateWindowW(L"EDIT", L"", styleMulti, 0, 0, 0, 0, m_hWnd, (HMENU)102, m_hInstance, nullptr);
    m_hEditPorts = CreateWindowW(L"EDIT", L"9000", styleMulti, 0, 0, 0, 0, m_hWnd, (HMENU)103, m_hInstance, nullptr);
    m_hEditLocal = CreateWindowW(L"EDIT", L"9000", styleSingle, 0, 0, 0, 0, m_hWnd, (HMENU)104, m_hInstance, nullptr);

    int dpi = GetDpiForWindow(m_hWnd);

    int fontHeight = -MulDiv(10, dpi, 72);

    HFONT hFont = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    SendMessageW(m_hEditName, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(m_hEditIPs, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(m_hEditPorts, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(m_hEditLocal, WM_SETFONT, (WPARAM)hFont, TRUE);

    LogStatus(L"UI initialized. enter display name and target IPs.");
}

void SetupWindow::UpdateLayout()
{
    if (!m_hWnd)
        return;
    RECT rc;
    GetClientRect(m_hWnd, &rc);
    float w = (float)(rc.right - rc.left);
    float h = (float)(rc.bottom - rc.top);
    float dpi = GetDpiForWindow(m_hWnd) / 96.0f;

    float margin = 20.0f * dpi;
    float titleH = 45.0f;
    float topY = titleH + 10.0f * dpi;

    float inputH = 32.0f * dpi;
    float labelH = 20.0f * dpi;

    float btnCloseW = 45.0f * dpi;
    m_rectBtnClose = D2D1::RectF(w - btnCloseW, 0, w, titleH);

    auto PlaceControl = [&](HWND hCtrl, D2D1_RECT_F &rect, float x, float y, float cw, float ch)
    {
        rect = D2D1::RectF(x, y, x + cw, y + ch);

        float padX = 6.0f * dpi;
        float padY = 4.0f * dpi;
        SetWindowPos(hCtrl, nullptr, (int)(x + padX), (int)(y + padY),
                     (int)(cw - padX * 2), (int)(ch - padY * 2), SWP_NOZORDER);
    };

    PlaceControl(m_hEditName, m_rectName, margin, topY + labelH, w - margin * 2, inputH);

    float midY = m_rectName.bottom + margin;
    float multiH = 120.0f * dpi;
    float halfW = (w - (margin * 3)) / 2.0f;

    PlaceControl(m_hEditIPs, m_rectIPs, margin, midY + labelH, halfW, multiH);
    PlaceControl(m_hEditPorts, m_rectPorts, margin + halfW + margin, midY + labelH, halfW, multiH);

    float localY = m_rectIPs.bottom + margin;
    PlaceControl(m_hEditLocal, m_rectLocal, margin, localY + labelH, 160.0f * dpi, inputH);

    float btnY = localY + labelH + inputH + margin * 1.5f;
    float btnW = 140.0f * dpi;
    float btnH = 36.0f * dpi;

    m_rectBtnSync = D2D1::RectF(margin, btnY, margin + btnW, btnY + btnH);
    m_rectBtnCancel = D2D1::RectF(w - margin - btnW, btnY, w - margin, btnY + btnH);

    float contW = 120.0f * dpi;
    m_rectBtnContinue = D2D1::RectF((w - contW) / 2.0f, btnY, (w + contW) / 2.0f, btnY + btnH);
}

LRESULT CALLBACK SetupWindow::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SetupWindow *pThis = nullptr;
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT *pCreate = (CREATESTRUCT *)lParam;
        pThis = (SetupWindow *)pCreate->lpCreateParams;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
    }
    else
    {
        pThis = (SetupWindow *)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    }

    if (pThis)
        return pThis->WndProc(hWnd, msg, wParam, lParam);
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT SetupWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
        OnPaint();
        ValidateRect(hWnd, nullptr);
        return 0;

    case WM_SIZE:
        if (m_swapChain)
        {
            DiscardDeviceResources();
            CreateDeviceResources();
        }
        UpdateLayout();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(240, 240, 240));
        SetBkColor(hdc, RGB(46, 46, 46));
        return (LRESULT)m_hbrEditBg;
    }

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (PtInRectF(m_rectBtnSync, x, y))
        {
            m_pressSync = true;
            SetCapture(hWnd);
            OnPaint();
        }
        else if (PtInRectF(m_rectBtnCancel, x, y))
        {
            m_pressCancel = true;
            SetCapture(hWnd);
            OnPaint();
        }
        else if (PtInRectF(m_rectBtnClose, x, y))
        {
            m_pressClose = true;
            SetCapture(hWnd);
            OnPaint();
        }
        else if (y < 40)
        {
            m_isDragging = true;
            GetCursorPos(&m_dragStartPoint);
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (GetCapture() == hWnd)
            ReleaseCapture();
        m_isDragging = false;

        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (m_pressSync && PtInRectF(m_rectBtnSync, x, y))
        {
            PerformSync();
        }

        if (m_pressCancel && PtInRectF(m_rectBtnCancel, x, y))
        {
            m_isSuccess = false;
            m_isRunning = false;
            PostQuitMessage(0);
        }

        if (m_pressClose && PtInRectF(m_rectBtnClose, x, y))
        {
            m_isSuccess = false;
            m_isRunning = false;
            PostQuitMessage(0);
        }

        m_pressSync = false;
        m_pressCancel = false;
        m_pressClose = false;
        OnPaint();
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (m_isDragging)
        {
            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - m_dragStartPoint.x;
            int dy = pt.y - m_dragStartPoint.y;
            RECT wRc;
            GetWindowRect(hWnd, &wRc);
            SetWindowPos(hWnd, nullptr, wRc.left + dx, wRc.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            m_dragStartPoint = pt;
            return 0;
        }

        bool hoverS = PtInRectF(m_rectBtnSync, x, y);
        bool hoverC = PtInRectF(m_rectBtnCancel, x, y);
        bool hoverCl = PtInRectF(m_rectBtnClose, x, y);

        if (hoverS != m_hoverSync || hoverC != m_hoverCancel || hoverCl != m_hoverClose)
        {
            m_hoverSync = hoverS;
            m_hoverCancel = hoverC;
            m_hoverClose = hoverCl;
            OnPaint();

            TRACKMOUSEEVENT tme = {sizeof(tme)};
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;
            TrackMouseEvent(&tme);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        m_hoverSync = false;
        m_hoverCancel = false;
        m_hoverClose = false;
        OnPaint();
        return 0;

    case WM_MOUSEWHEEL:
    {
        short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int step = 3;
        if (delta > 0)
            m_logOffset = std::max(0, m_logOffset - step);
        else
            m_logOffset = std::max(0, m_logOffset + step);
        OnPaint();
        return 0;
    }
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HRESULT SetupWindow::CreateDeviceIndependentResources()
{
    HRESULT hr = S_OK;
    D2D1_FACTORY_OPTIONS options = {};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options, &m_d2dFactory);
    if (FAILED(hr))
        return hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(m_dWriteFactory.GetAddressOf()));

    m_dWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &m_tfLabel);
    m_dWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0f, L"", &m_tfInput);
    m_dWriteFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"", &m_tfLog);
    m_dWriteFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"", &m_tfTitle);

    return S_OK;
}

HRESULT SetupWindow::CreateDeviceResources()
{
    if (m_d3dDevice)
        return S_OK;

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained;

    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags, featureLevels, 2, D3D11_SDK_VERSION, &m_d3dDevice, &obtained, &m_d3dContext);

    ComPtr<IDXGIDevice> dxgiDevice;
    m_d3dDevice.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(__uuidof(IDXGIFactory2), &dxgiFactory);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    hr = dxgiFactory->CreateSwapChainForHwnd(m_d3dDevice.Get(), m_hWnd, &sd, nullptr, nullptr, &m_swapChain);

    ComPtr<IDXGISurface> dxgiBackBuffer;
    m_swapChain->GetBuffer(0, __uuidof(IDXGISurface), &dxgiBackBuffer);

    hr = m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice);
    m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    m_d2dContext->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &props, &m_d2dTargetBitmap);
    m_d2dContext->SetTarget(m_d2dTargetBitmap.Get());

    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f), &m_brushBg);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.18f, 0.18f, 1.0f), &m_brushInput);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f, 1.0f), &m_brushText);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.7f, 0.7f, 0.7f, 1.0f), &m_brushLabel);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.5f, 0.8f, 1.0f), &m_brushAccent);
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.1f), &m_brushButtonHover);

    return S_OK;
}

void SetupWindow::DiscardDeviceResources()
{
    m_d2dContext.Reset();
    m_d2dTargetBitmap.Reset();
    m_swapChain.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

void SetupWindow::DrawTitleBar(float width, float titleH)
{

    m_d2dContext->FillRectangle(D2D1::RectF(0, 0, width, titleH), m_brushBg.Get());

    ComPtr<ID2D1SolidColorBrush> sep;
    m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f), &sep);
    m_d2dContext->DrawLine(D2D1::Point2F(0, titleH - 0.5f), D2D1::Point2F(width, titleH - 0.5f), sep.Get(), 1.0f);

    float dpi = GetDpiForWindow(m_hWnd) / 96.0f;
    m_d2dContext->DrawTextW(L"P2P Configuration", 22, m_tfTitle.Get(),
                            D2D1::RectF(20 * dpi, 10 * dpi, width - 60 * dpi, 40 * dpi), m_brushText.Get());

    float closeSz = 45.0f * dpi;
    m_rectBtnClose = D2D1::RectF(width - closeSz, 0, width, titleH);

    if (m_hoverClose)
    {
        ComPtr<ID2D1SolidColorBrush> brushRed;
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.8f, 0.1f, 0.1f, m_pressClose ? 0.8f : 0.6f), &brushRed);
        m_d2dContext->FillRectangle(m_rectBtnClose, brushRed.Get());
    }

    float pad = 15.0f * dpi;
    D2D1_RECT_F r = m_rectBtnClose;
    m_d2dContext->DrawLine(D2D1::Point2F(r.left + pad, r.top + pad), D2D1::Point2F(r.right - pad, r.bottom - pad), m_brushText.Get(), 2.0f);
    m_d2dContext->DrawLine(D2D1::Point2F(r.left + pad, r.bottom - pad), D2D1::Point2F(r.right - pad, r.top + pad), m_brushText.Get(), 2.0f);
}

void SetupWindow::DrawInputFields()
{
    auto DrawField = [&](const std::wstring &label, const D2D1_RECT_F &rect)
    {
        D2D1_RECT_F labelRect = D2D1::RectF(rect.left, rect.top - 25, rect.right, rect.top);
        m_d2dContext->DrawTextW(label.c_str(), (UINT32)label.size(), m_tfLabel.Get(),
                                labelRect, m_brushLabel.Get());

        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(rect, 4.0f, 4.0f), m_brushInput.Get());
    };

    DrawField(L"Display name", m_rectName);
    DrawField(L"Target IPs (one per line)", m_rectIPs);
    DrawField(L"Target ports (defaults to 9000)", m_rectPorts);
    DrawField(L"Local port", m_rectLocal);
}

void SetupWindow::DrawButtons()
{
    auto DrawBtn = [&](const std::wstring &txt, D2D1_RECT_F r, bool hover, bool press, bool primary)
    {
        ID2D1Brush *bg = primary ? m_brushAccent.Get() : m_brushInput.Get();

        m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4), bg);

        if (hover)
            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4), m_brushButtonHover.Get());

        if (press)
        {
            ComPtr<ID2D1SolidColorBrush> brushPress;
            m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.2f), &brushPress);
            m_d2dContext->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4), brushPress.Get());
        }

        float btnW = r.right - r.left;
        float btnH = r.bottom - r.top;
        ComPtr<IDWriteTextLayout> layout;
        m_dWriteFactory->CreateTextLayout(txt.c_str(), (UINT32)txt.size(), m_tfLabel.Get(), btnW, btnH, &layout);
        layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_d2dContext->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), m_brushText.Get());
    };

    DrawBtn(L"Synchronize", m_rectBtnSync, m_hoverSync, m_pressSync, true);
    DrawBtn(L"Cancel", m_rectBtnCancel, m_hoverCancel, m_pressCancel, false);
}

void SetupWindow::DrawLog(float width, float startY)
{
    float dpi = GetDpiForWindow(m_hWnd) / 96.0f;
    m_d2dContext->DrawTextW(L"Status log:", 11, m_tfLabel.Get(),
                            D2D1::RectF(20 * dpi, startY - 20 * dpi, 400 * dpi, startY), m_brushLabel.Get());

    float curY = startY;
    float lineHeight = 16.0f * dpi;

    std::lock_guard<std::mutex> lk(m_mutex);
    int totalLines = (int)m_statusLog.size();
    int maxVisible = 10;
    int start = std::max(0, std::min(totalLines - 1, m_logOffset));
    int count = std::min(maxVisible, totalLines - start);

    for (int i = 0; i < count; ++i)
    {
        if (start + i < totalLines)
        {
            std::wstring line = m_statusLog[start + i];
            m_d2dContext->DrawTextW(line.c_str(), (UINT32)line.size(), m_tfLog.Get(),
                                    D2D1::RectF(20 * dpi, curY, width, curY + lineHeight), m_brushText.Get());
            curY += lineHeight;
        }
    }
}

void SetupWindow::OnPaint()
{
    if (!m_d2dContext)
        return;
    m_d2dContext->BeginDraw();
    m_d2dContext->Clear(D2D1::ColorF(0.15f, 0.15f, 0.15f, 1.0f));

    float dpi = GetDpiForWindow(m_hWnd) / 96.0f;
    D2D1_SIZE_F size = m_d2dContext->GetSize();

    DrawTitleBar(size.width, 45.0f);
    DrawInputFields();
    DrawButtons();

    float logY = m_rectBtnSync.bottom + 25.0f * dpi;
    DrawLog(size.width, logY);

    HRESULT hr = m_d2dContext->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET)
        DiscardDeviceResources();
    if (m_swapChain)
        m_swapChain->Present(1, 0);
}

void SetupWindow::LogStatus(const std::wstring &msg)
{
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_statusLog.push_back(msg);
        if ((int)m_statusLog.size() > 1000)
        {
            m_statusLog.erase(m_statusLog.begin(), m_statusLog.begin() + ((int)m_statusLog.size() - 1000));
        }
        m_logOffset = std::max(0, (int)m_statusLog.size() - 10);
    }
    if (IsWindow(m_hWnd))
    {
        InvalidateRect(m_hWnd, nullptr, FALSE);
    }
}

void SetupWindow::PerformSync()
{
    wchar_t tmp[4096];
    GetWindowTextW(m_hEditName, tmp, 1024);
    std::wstring name = trim_ws_local(tmp);
    if (name.empty())
    {
        MessageBoxW(m_hWnd, L"Display name required", L"Error", MB_OK);
        return;
    }

    GetWindowTextW(m_hEditLocal, tmp, 128);
    int localPort = _wtoi(tmp);
    if (localPort <= 0 || localPort > 65535)
        localPort = 9000;

    GetWindowTextW(m_hEditIPs, tmp, 4096);
    std::wstringstream ss(tmp);
    std::wstring line;
    m_targetIPs.clear();
    while (std::getline(ss, line))
    {
        line = trim_ws_local(line);
        if (!line.empty())
            m_targetIPs.push_back(line);
    }

    GetWindowTextW(m_hEditPorts, tmp, 4096);
    std::wstringstream ssp(tmp);
    m_targetPorts.clear();
    while (std::getline(ssp, line))
    {
        line = trim_ws_local(line);
        if (!line.empty())
        {
            int p = _wtoi(line.c_str());
            m_targetPorts.push_back((unsigned short)(p > 0 ? p : 9000));
        }
    }
    if (m_targetPorts.empty())
        m_targetPorts.resize(m_targetIPs.size(), 9000);
    else if (m_targetPorts.size() < m_targetIPs.size())
    {
        unsigned short last = m_targetPorts.back();
        m_targetPorts.resize(m_targetIPs.size(), last);
    }

    InitNetwork();

    LogStatus(L"Starting mesh network...");

    global_net_engine->startListening(localPort);

    for (size_t i = 0; i < m_targetIPs.size(); ++i)
    {
        std::string ip = WStringToString(m_targetIPs[i]);
        int port = m_targetPorts[i];

        LogStatus(L"Adding peer: " + m_targetIPs[i]);
        global_net_engine->addPersistentPeer(ip, port);
    }

    LogStatus(L"Network configured launching chat.");

    m_isSuccess = true;
    m_isRunning = false;
}

bool SetupWindow::PtInRectF(const D2D1_RECT_F &r, int x, int y)
{
    return x >= (int)r.left && x <= (int)r.right && y >= (int)r.top && y <= (int)r.bottom;
}