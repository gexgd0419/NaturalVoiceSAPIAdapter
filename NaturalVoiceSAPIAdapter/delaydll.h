#pragma once

class DllDelayLoadError
{
	DWORD m_win32ErrCode;
public:
	DllDelayLoadError(DWORD dwWin32ErrCode = GetLastError()) : m_win32ErrCode(dwWin32ErrCode) {}
	DWORD code() const { return m_win32ErrCode; }
};