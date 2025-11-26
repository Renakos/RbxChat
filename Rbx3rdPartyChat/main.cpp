#include "pch.h"
#include "GlobalNetwork.h"   
#include "SetupWindow.h"     
#include "ChatWindow.h"      

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        MessageBoxW(nullptr, L"WSAStartup failed.", L"Fatal error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::wstring localNick;

    if (SetupWindow::Run(hInstance, localNick)) {

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {

            }
            else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        ChatWindow chat(hInstance);
        chat.SetLocalNick(localNick);

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int chatW = 320;
        int chatH = 480;
        int x = (screenW - chatW) / 2;
        int y = (screenH - chatH) / 2;

        if (chat.Create(x, y, chatW, chatH, nCmdShow)) {
            chat.Run();
        }
        else {
            MessageBoxW(nullptr, L"Failed to create chat window", L"Error", MB_OK);
        }
    }

    if (global_net_engine) {
        global_net_engine->stop();
        global_net_engine = nullptr;
    }
    WSACleanup();

    return 0;
}