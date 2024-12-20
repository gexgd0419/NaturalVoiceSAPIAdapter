﻿#pragma once

#ifndef STRICT
#define STRICT
#endif

#define NOMINMAX

#include "targetver.h"

#define _ATL_APARTMENT_THREADED

#define _ATL_NO_AUTOMATIC_NAMESPACE

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS	// 某些 CString 构造函数将是显式的


#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

#include "resource.h"
#include <atlbase.h>
#include <atlcom.h>
#include <atlctl.h>

#define RETONFAIL(hresult) if (HRESULT __hr = hresult; FAILED(__hr)) return __hr

extern WCHAR g_regModulePath[];
extern ATL::_ATL_REGMAP_ENTRY g_regEntries[];

#undef DECLARE_REGISTRY_RESOURCEID
#define DECLARE_REGISTRY_RESOURCEID(x)\
static HRESULT WINAPI UpdateRegistry(_In_ BOOL bRegister) throw()\
{ return ATL::_pAtlModule->UpdateRegistryFromResource(x, bRegister, g_regEntries); }

// C++ standard library headers
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <future>
#include <functional>
#include <system_error>
#include <format>
#include <queue>
#include <optional>
#include <map>
#include <set>
#include <mutex>
#include <condition_variable>

using ATL::CComPtr; // put this here to avoid 'CComPtr undeclared' in spddkhlp.h
#pragma warning(suppress : 4996) // 'GetVersionExW' deprecated

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <spddkhlp.h>
#include <ShObjIdl.h>

#include <winrt/windows.management.deployment.h>
#include <winrt/windows.applicationmodel.h>
#include <winrt/windows.system.userprofile.h>
#include <winrt/windows.foundation.collections.h>

// Third-party libraries
#include "AzacException.h"  // overrides Azure SDK's exception
#include <speechapi_cxx.h>
#define ASIO_STANDALONE 1
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>