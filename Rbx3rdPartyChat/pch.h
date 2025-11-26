
#pragma once

// windows maxros
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

// windows/system
#include <windows.h>
#include <windowsx.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <string>
#include <mutex>
#include <sstream>
#include <algorithm>
#include <thread>
#include <map>
#include <deque>
#include <iostream>
#include <set>
#include <commctrl.h> // subxlass
#pragma comment(lib, "comctl32.lib") 
using Microsoft::WRL::ComPtr;
// dirextx
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>

// boost
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/context.hpp>

// openssl
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/types.h>