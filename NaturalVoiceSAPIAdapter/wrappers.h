#pragma once
#include <winbase.h>
#include <winreg.h>

template <typename THandle, auto CloseFunc, THandle ZeroValue = THandle()>
class HandleWrapper
{
private:
	THandle m_handle;
public:
	HandleWrapper() noexcept : m_handle(ZeroValue) {}
	HandleWrapper(THandle handle) noexcept : m_handle(handle) {}
	~HandleWrapper() noexcept
	{
		if (m_handle != ZeroValue)
			CloseFunc(m_handle);
	}
	HandleWrapper(HandleWrapper&& wrapper) noexcept : m_handle(wrapper.Detach()) {}
	HandleWrapper& operator=(HandleWrapper&& wrapper) noexcept
	{
		Attach(wrapper.Detach());
		return *this;
	}

	operator THandle() const noexcept { return m_handle; }
	THandle* operator&() noexcept { return &m_handle; }
	const THandle* operator&() const noexcept { return &m_handle; }

	void Close() noexcept
	{
		if (m_handle != ZeroValue)
			CloseFunc(m_handle);
		m_handle = ZeroValue;
	}
	void Attach(THandle handle) noexcept
	{
		if (m_handle != ZeroValue)
			CloseFunc(m_handle);
		m_handle = handle;
	}
	THandle Detach() noexcept
	{
		THandle old = m_handle;
		m_handle = ZeroValue;
		return old;
	}
};

typedef HandleWrapper<HANDLE, CloseHandle> Handle;
typedef HandleWrapper<HANDLE, CloseHandle, INVALID_HANDLE_VALUE> HFile;
typedef HandleWrapper<HKEY, RegCloseKey> HKey;