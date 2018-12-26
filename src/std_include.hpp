#pragma once

#define BINARY_PAYLOAD_SIZE 0x0A000000

#pragma warning(push)
#pragma warning(disable: 4458)

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <WinSock2.h>
#include <corecrt_io.h>
#include <fcntl.h>

// min and max is required by gdi, therefore NOMINMAX won't work
#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <map>
#include <vector>
#include <mutex>
#include <queue>
#include <thread>
#include <fstream>
#include <utility>
#include <filesystem>

#include <zlib.h>
#include <diff.h>
#include <patch.h>
#include <tomcrypt.h>

#pragma warning(pop)

using namespace std::literals;

#pragma comment (lib, "gdiplus.lib")

#pragma warning(disable: 4100)

#include "resource.hpp"
