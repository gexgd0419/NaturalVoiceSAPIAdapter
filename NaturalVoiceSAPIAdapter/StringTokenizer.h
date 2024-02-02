#pragma once
#include <string_view>
#include <iterator>

template <typename CharT,
	typename DelimeterT = std::basic_string_view<CharT>,
	size_t (std::basic_string_view<CharT>::*FindFunc)(std::decay_t<DelimeterT>, size_t) const noexcept = &std::basic_string_view<CharT>::find,
	bool Reverse = false>
class StringTokenIterator
{
public:
	using iterator_category = std::forward_iterator_tag;
	using value_type = std::basic_string_view<CharT>;
	//using difference_type = ptrdiff_t;
	using pointer = const std::basic_string_view<CharT>*;
	using reference = const std::basic_string_view<CharT>&;
private:
	value_type str, current;
	DelimeterT delimeter;
	static constexpr bool IsCharType = std::is_same_v<CharT, DelimeterT>;
	constexpr size_t delim_size() const noexcept
	{
		if constexpr (IsCharType)
			return 1;
		else
			return std::size(delimeter);
	}
public:
	constexpr StringTokenIterator() noexcept : str(), delimeter(std::remove_cvref_t<DelimeterT>{}) {}
	constexpr StringTokenIterator(const value_type& str, const DelimeterT& delimeter) noexcept
		: str(str), delimeter(delimeter)
	{
		if constexpr (!IsCharType)
		{
			if (std::empty(delimeter))
			{
				// If delimeter is empty, find() will return 0 immediately
				// We just return the whole string in this case
				current = str;
				return;
			}
		}
		if constexpr (Reverse)
		{
			size_t pos = (str.*FindFunc)(delimeter, str.npos);
			if (pos == str.npos)
				current = str;
			else
				current = str.substr(pos + delim_size());
		}
		else
		{
			size_t pos = (str.*FindFunc)(delimeter, 0);
			if (pos == str.npos)
				current = str;
			else
				current = str.substr(0, pos);
		}
	}
	constexpr StringTokenIterator& operator++() noexcept
	{
		if (!current.data())
			return *this;

		size_t startpos;
		if constexpr (Reverse)
		{
			// Current string is after the previous delimeter
			startpos = static_cast<size_t>(current.data() - str.data());
			// Skip to before the previous delimeter
			// But first check if we can, since startpos is unsigned
			if (startpos == delim_size()) // the string begins with a delimeter
			{
				current = str.substr(0, 0); // an empty range at the start of the string
				return *this;
			}
			else if (startpos == 0) // we are at the start; no more results
			{
				current = {};
				return *this;
			}
			startpos -= delim_size() + 1; // +1 to skip the current char
		}
		else
		{
			// Current string is before the next delimeter
			// Skip the current string and the next delimeter
			startpos = static_cast<size_t>(current.data() - str.data()) + current.size() + delim_size();
			if (startpos > str.size())
			{
				current = {};
				return *this;
			}
		}

		size_t nextpos = (str.*FindFunc)(delimeter, startpos);

		if constexpr (Reverse)
		{
			if (nextpos != str.npos)
				current = str.substr(nextpos + delim_size(), startpos - nextpos);
			else
				current = str.substr(0, startpos + 1);
		}
		else
		{
			if (nextpos != str.npos)
				current = str.substr(startpos, nextpos - startpos);
			else
				current = str.substr(startpos);
		}

		return *this;
	}
	constexpr StringTokenIterator operator++(int) noexcept
	{
		auto temp = *this;
		++*this;
		return temp;
	}
	constexpr bool operator==(const StringTokenIterator& oth) const noexcept
	{
		return current.data() == oth.current.data();
	}
	constexpr bool operator!=(const StringTokenIterator& oth) const noexcept { return !operator==(oth); }
	constexpr reference operator*() const noexcept { return current; }
	constexpr pointer operator->() const noexcept { return &current; }
};

template <class Iter>
class IteratorRange
{
	Iter iter;
public:
	using iterator = Iter;
	template <typename... Args>
	constexpr IteratorRange(Args&& ...args) noexcept : iter(std::forward<Args>(args)...) {}
	constexpr Iter begin() noexcept { return iter; }
	constexpr static Iter end() noexcept { return {}; }
};

template <typename CharT>
constexpr IteratorRange<StringTokenIterator<CharT, CharT>> TokenizeString
	(std::basic_string_view<CharT> str, CharT delimeter)
{
	return { str, delimeter };
}

template <typename CharT>
constexpr IteratorRange<StringTokenIterator<CharT>> TokenizeString
	(std::basic_string_view<CharT> str, std::basic_string_view<CharT> delimeter)
{
	return { str, delimeter };
}

template <typename CharT, size_t N>
constexpr IteratorRange<StringTokenIterator<CharT, const CharT (&)[N]>> TokenizeString
	(std::basic_string_view<CharT> str, const CharT (&delimeter)[N])
{
	return { str, delimeter };
}
