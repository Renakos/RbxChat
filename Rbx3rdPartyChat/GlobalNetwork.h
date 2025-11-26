#pragma once
#include "NetworkEngine.h"
#include <memory>
#include <string>
#include <windows.h>
// idk  why this  isn't a hpp file,  but  ok
inline std::string WStringToString(const std::wstring &wstr)
{
    if (wstr.empty())
        return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

inline std::wstring StringToWString(const std::string &s)
{
    if (s.empty())
        return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], size_needed);
    return w;
}

extern std::shared_ptr<NetworkEngine> global_net_engine;

inline void InitNetwork()
{
    if (!global_net_engine)
    {
        global_net_engine = std::make_shared<NetworkEngine>();
        global_net_engine->start();
    }
}