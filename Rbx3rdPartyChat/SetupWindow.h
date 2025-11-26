#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <mutex>
#include <d2d1.h>
#include <dcommon.h>
#include "GlobalNetwork.h"

class SetupWindow
{
public:
    static bool Run(HINSTANCE hInstance, std::wstring &outNick);

    SetupWindow(HINSTANCE hInstance);
    ~SetupWindow();

    bool ShowModal(std::wstring &outNick);

private:
    void DrawTitleBar(float width, float titleH);
    void DrawInputFields();
    void DrawButtons();
    void DrawLog(float width, float startY);

    static LRESULT CALLBACK StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HRESULT CreateDeviceIndependentResources();
    HRESULT CreateDeviceResources();
    void DiscardDeviceResources();
    void OnPaint();

    void CreateChildControls(int x, int y, int w, int h);
    void UpdateLayout();

    void PerformSync();
    void LogStatus(const std::wstring &msg);

    bool PtInRectF(const D2D1_RECT_F &r, int x, int y);

    bool IsPeerListening(const std::string &ip, unsigned short port, int timeoutMs);

private:
    HINSTANCE m_hInstance;
    HWND m_hWnd;

    HWND m_hEditName;
    HWND m_hEditIPs;
    HWND m_hEditPorts;
    HWND m_hEditLocal;
    HBRUSH m_hbrEditBg;

    bool m_isRunning = false;
    bool m_isSuccess = false;

    std::vector<std::wstring> m_statusLog;
    int m_logOffset = 0;

    std::vector<std::wstring> m_targetIPs;
    std::vector<unsigned short> m_targetPorts;

    bool m_continueEnabled = false;

    Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID2D1Factory1> m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device> m_d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> m_d2dTargetBitmap;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dWriteFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfLabel;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfInput;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfLog;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_tfTitle;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushBg;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushInput;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushLabel;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushAccent;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brushButtonHover;

    D2D1_RECT_F m_rectBtnClose;
    D2D1_RECT_F m_rectBtnSync;
    D2D1_RECT_F m_rectBtnCancel;
    D2D1_RECT_F m_rectBtnContinue;
    D2D1_RECT_F m_rectName;
    D2D1_RECT_F m_rectIPs;
    D2D1_RECT_F m_rectPorts;
    D2D1_RECT_F m_rectLocal;

    bool m_hoverClose = false;
    bool m_pressClose = false;
    bool m_hoverSync = false;
    bool m_pressSync = false;
    bool m_hoverCancel = false;
    bool m_pressCancel = false;
    bool m_hoverContinue = false;
    bool m_pressContinue = false;

    bool m_isDragging = false;
    POINT m_dragStartPoint{};

    std::mutex m_mutex;
};