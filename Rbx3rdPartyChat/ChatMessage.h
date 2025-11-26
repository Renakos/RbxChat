#pragma once
#include <string>

struct ChatMessage {
    std::wstring name;
    std::wstring text;
    bool isMine;
};