#pragma once

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

#include <memory>
#include <string>
#include <vector>

using ATL::CComPtr; // put this here to avoid 'CComPtr undeclared' in spddkhlp.h
#pragma warning(suppress : 4996) // 'GetVersionExW' deprecated
#include <spddkhlp.h>