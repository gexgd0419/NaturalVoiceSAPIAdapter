// header.h: 标准系统包含文件的包含文件，
// 或特定于项目的包含文件
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容

// Fix for error C2760, before any COM header files
// see: https://stackoverflow.com/a/62349505/20413916
typedef struct IUnknown IUnknown;

#include <windows.h>
#include <Shlwapi.h>
#pragma comment (lib, "shlwapi.lib")

// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <detours.h>
#pragma comment (lib, "detours.lib")

#include <vector>
#include <string>
