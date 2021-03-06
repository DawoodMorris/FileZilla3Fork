#include "libfilezilla/encode.hpp"

namespace fz {

namespace {
template<typename DataContainer>
std::string base64_encode_impl(DataContainer const& in, base64_type type, bool pad)
{
	static_assert(sizeof(typename DataContainer::value_type) == 1, "Bad container type");

	std::string::value_type const* const base64_chars =
		 (type == base64_type::standard)
			? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
			: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

	std::string ret;

	size_t len = in.size();
	size_t pos{};

	ret.reserve(((len + 2) / 3) * 4);

	while (len >= 3) {
		len -= 3;
		auto const c1 = static_cast<unsigned char>(in[pos++]);
		auto const c2 = static_cast<unsigned char>(in[pos++]);
		auto const c3 = static_cast<unsigned char>(in[pos++]);

		ret += base64_chars[(c1 >> 2) & 0x3fu];
		ret += base64_chars[((c1 & 0x3u) << 4) | ((c2 >> 4) & 0xfu)];
		ret += base64_chars[((c2 & 0xfu) << 2) | ((c3 >> 6) & 0x3u)];
		ret += base64_chars[(c3 & 0x3fu)];
	}
	if (len) {
		auto const c1 = static_cast<unsigned char>(in[pos++]);
		ret += base64_chars[(c1 >> 2) & 0x3fu];
		if (len == 2) {
			auto const c2 = static_cast<unsigned char>(in[pos++]);
			ret += base64_chars[((c1 & 0x3u) << 4) | ((c2 >> 4) & 0xfu)];
			ret += base64_chars[(c2 & 0xfu) << 2];
		}
		else {
			ret += base64_chars[(c1 & 0x3u) << 4];
			if (pad) {
				ret += '=';
			}
		}
		if (pad) {
			ret += '=';
		}
	}

	return ret;
}
}

std::string base64_encode(std::string const& in, base64_type type, bool pad)
{
	return base64_encode_impl(in, type, pad);
}

std::string base64_encode(std::vector<uint8_t> const& in, base64_type type, bool pad)
{
	return base64_encode_impl(in, type, pad);
}

std::string base64_decode(std::string const& in)
{
	unsigned char const chars[256] =
	{
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x80, 0x80, 0xff, 0x80, 0x80, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0x3e, 0xff, 0x3f,
	    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff, 0xff, 0x40, 0xff, 0xff,
	    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e,
	    0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0x3f,
	    0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
	    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};

	std::string ret;
	ret.reserve((in.size() / 4) * 3);

	size_t pos{};
	size_t len = in.size();

	// Trim trailing whitespace
	while (len && chars[static_cast<unsigned char>(in[len - 1])] == 0x80) {
		--len;
	}

	auto next = [&]() {
		while (pos < len) {
			auto c = chars[static_cast<unsigned char>(in[pos++])];
			if (c != 0x80u) {
				return c;
			}
		}
		return static_cast<unsigned char>(0x40u);
	};

	while (pos < len) {
		auto c1 = next();
		auto c2 = next();
		auto c3 = next();
		auto c4 = next();

		if (c1 == 0xff || c1 == 0x40 ||
		    c2 == 0xff || c2 == 0x40 ||
		    c3 == 0xff || c4 == 0xff)
		{
			// Bad input
			return std::string();
		}

		if (c4 == 0x40) {
			// Pad
			if (pos < len) {
				// Not at end of string
				return std::string();
			}
			ret += (c1 << 2) | ((c2 >> 4) & 0x3);

			if (c3 != 0x40) {
				ret += ((c2 & 0xf) << 4) | ((c3 >> 2) & 0xf);
			}
		}
		else {
			if (c3 == 0x40) {
				// Bad input
				return std::string();
			}

			ret += (c1 << 2) | ((c2 >> 4) & 0x3);
			ret += ((c2 & 0xf) << 4) | ((c3 >> 2) & 0xf);
			ret += ((c3 & 0x3) << 6) | c4;
		}
	}

	return ret;
}


std::string percent_encode(std::string const& s, bool keep_slashes)
{
	std::string ret;
	ret.reserve(s.size());

	for (auto const& c : s) {
		if (!c) {
			break;
		}
		else if ((c >= '0' && c <= '9') ||
		    (c >= 'a' && c <= 'z') ||
		    (c >= 'A' && c <= 'Z') ||
		    c == '-' || c == '.' || c == '_' || c == '~')
		{
			ret += c;
		}
		else if (c == '/' && keep_slashes) {
			ret += c;
		}
		else {
			ret += '%';
			ret += int_to_hex_char<char, false>(static_cast<unsigned char>(c) >> 4);
			ret += int_to_hex_char<char, false>(c & 0xf);
		}
	}

	return ret;
}

std::string percent_encode(std::wstring const& s, bool keep_slashes)
{
	return percent_encode(to_utf8(s), keep_slashes);
}

std::wstring percent_encode_w(std::wstring const& s, bool keep_slashes)
{
	return to_wstring(percent_encode(s, keep_slashes));
}

std::string percent_decode(std::string const& s)
{
	std::string ret;
	ret.reserve(s.size());

	char const* c = s.c_str();
	while (*c) {
		if (*c == '%') {
			int high = hex_char_to_int(*(++c));
			if (high == -1) {
				return std::string();
			}
			int low = hex_char_to_int(*(++c));
			if (low == -1) {
				return std::string();
			}

			// Special feature: Disallow
			if (!high && !low) {
				return std::string();
			}
			ret.push_back(static_cast<char>(static_cast<uint8_t>((high << 4) + low)));
		}
		else {
			ret.push_back(*c);
		}
		++c;
	}

	return ret;
}

}
