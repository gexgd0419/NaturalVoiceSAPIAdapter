#pragma once
#include <memory>
#include <system_error>
#include <map>
#include <WinBase.h>
#include "wrappers.h"

// An error_category for retrieving error messages from a specific DLL.
// Can fall back to system error messages.
class system_library_category_impl : public std::error_category
{
private:
	std::string _name, _filename;

public:
	system_library_category_impl(const std::string& name, const std::string& filename)
		: _name(name), _filename(filename) {}
	const char* name() const noexcept override { return _name.c_str(); }
	std::string message(int ev) const override
	{
		HandleWrapper<HMODULE, FreeLibrary> hLib = LoadLibraryA(_filename.c_str());
		HandleWrapper<char*, LocalFree> pMsg = nullptr;

		if (FormatMessageA(
			FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER,
			hLib, ev, LANG_USER_DEFAULT,
			reinterpret_cast<LPSTR>(&pMsg), 0, nullptr) == 0)
		{
			return "unknown error";
		}

		std::string str(pMsg);

		// Remove trailing \r\n's
		auto it = str.end(), itBegin = str.begin();
		while (it != itBegin)
		{
			--it;
			if (!isspace(*it))
			{
				++it;
				break;
			}
		}

		str.erase(it);
		return str;
	}
};

inline const system_library_category_impl& system_library_category(const std::string& name, const std::string& filename)
{
	// Each error_category has only one instance.
	// Use a map to store one instance for each library.
	static std::map<std::string, system_library_category_impl> map;
	auto it = map.try_emplace(name, name, filename).first;
	return it->second;
}

inline const system_library_category_impl& system_library_category(const std::string& name)
{
	return system_library_category(name, name);
}