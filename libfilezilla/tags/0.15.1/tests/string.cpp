#include "libfilezilla/encode.hpp"
#include "libfilezilla/string.hpp"

#include "test_utils.hpp"
/*
 * This testsuite asserts the correctness of the
 * string functions
 */

class string_test final : public CppUnit::TestFixture
{
	CPPUNIT_TEST_SUITE(string_test);
	CPPUNIT_TEST(test_conversion);
	CPPUNIT_TEST(test_conversion2);
	CPPUNIT_TEST(test_conversion_utf8);
	CPPUNIT_TEST(test_base64);
	CPPUNIT_TEST(test_trim);
	CPPUNIT_TEST(test_strtok);
	CPPUNIT_TEST(test_startsendswith);
	CPPUNIT_TEST_SUITE_END();

public:
	void setUp() {}
	void tearDown() {}

	void test_conversion();
	void test_conversion2();
	void test_conversion_utf8();
	void test_base64();
	void test_trim();
	void test_strtok();
	void test_startsendswith();
};

CPPUNIT_TEST_SUITE_REGISTRATION(string_test);

void string_test::test_conversion()
{
	std::string const s("hello world!");

	std::wstring const w = fz::to_wstring(s);

	CPPUNIT_ASSERT_EQUAL(s.size(), w.size());

	for (size_t i = 0; i < s.size(); ++i) {
		CPPUNIT_ASSERT_EQUAL(static_cast<wchar_t>(s[i]), w[i]);
	}


	std::string const s2 = fz::to_string(s);

	CPPUNIT_ASSERT_EQUAL(s, s2);
}

void string_test::test_conversion2()
{
	wchar_t const p[] = { 'M', 'o', 't', 0xf6, 'r', 'h', 'e', 'a', 'd', 0 };
	std::wstring const w(p);

	std::string const s = fz::to_string(w);

	CPPUNIT_ASSERT(s.size() >= w.size());

	std::wstring const w2 = fz::to_wstring(s);

	ASSERT_EQUAL(w, w2);
}

void string_test::test_conversion_utf8()
{
	wchar_t const p[] = { 'M', 'o', 't', 0xf6, 'r', 'h', 'e', 'a', 'd', 0 };
	unsigned char const p_utf8[] = { 'M', 'o', 't', 0xc3, 0xb6, 'r', 'h', 'e', 'a', 'd', 0 };

	std::wstring const w(p);
	std::string const u(reinterpret_cast<char const*>(p_utf8));

	std::string const s = fz::to_utf8(w);

	CPPUNIT_ASSERT(s.size() >= w.size());

	ASSERT_EQUAL(s, u);

	std::wstring const w2 = fz::to_wstring_from_utf8(s);

	ASSERT_EQUAL(w, w2);
}

void string_test::test_base64()
{
	CPPUNIT_ASSERT_EQUAL(std::string(""),         fz::base64_encode(""));
	CPPUNIT_ASSERT_EQUAL(std::string("Zg=="),     fz::base64_encode("f"));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm8="),     fz::base64_encode("fo"));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm9v"),     fz::base64_encode("foo"));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm9vbA=="), fz::base64_encode("fool"));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm9vbHM="), fz::base64_encode("fools"));

	CPPUNIT_ASSERT_EQUAL(std::string("Zg"),     fz::base64_encode("f", fz::base64_type::standard, false));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm8"),     fz::base64_encode("fo", fz::base64_type::standard, false));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm9vbA"), fz::base64_encode("fool", fz::base64_type::standard, false));
	CPPUNIT_ASSERT_EQUAL(std::string("Zm9vbHM"), fz::base64_encode("fools", fz::base64_type::standard, false));

	CPPUNIT_ASSERT_EQUAL(std::string("AAECA/3+/w=="), fz::base64_encode(std::string({0, 1, 2, 3, '\xfd', '\xfe', '\xff'})));

	CPPUNIT_ASSERT_EQUAL(std::string("AAECA_3-_w=="), fz::base64_encode(std::string({0, 1, 2, 3, '\xfd', '\xfe', '\xff'}), fz::base64_type::url));

	// decode
	CPPUNIT_ASSERT_EQUAL(std::string(""),      fz::base64_decode(""));
	CPPUNIT_ASSERT_EQUAL(std::string("f"),     fz::base64_decode("Zg=="));
	CPPUNIT_ASSERT_EQUAL(std::string("fo"),    fz::base64_decode("Zm8="));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"),   fz::base64_decode("Zm9v"));
	CPPUNIT_ASSERT_EQUAL(std::string("fool"),  fz::base64_decode("Zm9vbA=="));
	CPPUNIT_ASSERT_EQUAL(std::string("fools"), fz::base64_decode("Zm9vbHM="));

	CPPUNIT_ASSERT_EQUAL(std::string("f"),     fz::base64_decode("Zg"));
	CPPUNIT_ASSERT_EQUAL(std::string("fo"),    fz::base64_decode("Zm8"));
	CPPUNIT_ASSERT_EQUAL(std::string("fool"),  fz::base64_decode("Zm9vbA"));
	CPPUNIT_ASSERT_EQUAL(std::string("fools"), fz::base64_decode("Zm9vbHM"));

	CPPUNIT_ASSERT_EQUAL(std::string({0, 1, 2, 3, '\xfd', '\xfe', '\xff'}), fz::base64_decode("AAECA/3+/w=="));
	CPPUNIT_ASSERT_EQUAL(std::string({0, 1, 2, 3, '\xfd', '\xfe', '\xff'}), fz::base64_decode("AAECA_3-_w"));

	// with whitespace
	CPPUNIT_ASSERT_EQUAL(std::string("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua."),
						 fz::base64_decode(" TG9yZW0gaXBzdW0gZG9sb3Igc2l0I\nGFtZXQsIGNvbnNlY3Rld\rHVyIGFkaXBpc2NpbmcgZWxpd \tCwgc2VkIGRvIGVpdXNtb2QgdGVtcG9yIGluY2lkaWR1bnQgdXQgbGFib3JlIGV0IGRvbG9yZSBtYWduYSBhbGlxdWEu "));

	// invalid
	CPPUNIT_ASSERT_EQUAL(std::string(""), fz::base64_decode("Zm9vbHM=="));
	CPPUNIT_ASSERT_EQUAL(std::string(""), fz::base64_decode("Zm9vb==="));
	CPPUNIT_ASSERT_EQUAL(std::string(""), fz::base64_decode("Zm9vbHM=Zg=="));
}

void string_test::test_trim()
{
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("foo")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string(" foo")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("\t foo")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("foo ")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("foo \n")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string(" foo\n")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("\t foo\r")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string(" foo  ")));
	CPPUNIT_ASSERT_EQUAL(std::string("foo"), fz::trimmed(std::string("\t foo  ")));
	CPPUNIT_ASSERT_EQUAL(std::string(""), fz::trimmed(std::string(" \t\r \n \t")));
}

void string_test::test_strtok()
{
	auto tokens = fz::strtok<std::string>("hello world", ' ');
	CPPUNIT_ASSERT_EQUAL(size_t(2), tokens.size());
	CPPUNIT_ASSERT_EQUAL(std::string("hello"), tokens[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("world"), tokens[1]);

	tokens = fz::strtok<std::string>(" hello   world  ", " eo");
	CPPUNIT_ASSERT_EQUAL(size_t(4), tokens.size());
	CPPUNIT_ASSERT_EQUAL(std::string("h"), tokens[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("ll"), tokens[1]);
	CPPUNIT_ASSERT_EQUAL(std::string("w"), tokens[2]);
	CPPUNIT_ASSERT_EQUAL(std::string("rld"), tokens[3]);

	tokens = fz::strtok<std::string>("a b c", ' ');
	CPPUNIT_ASSERT_EQUAL(size_t(3), tokens.size());
	CPPUNIT_ASSERT_EQUAL(std::string("a"), tokens[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("b"), tokens[1]);
	CPPUNIT_ASSERT_EQUAL(std::string("c"), tokens[2]);
}

void string_test::test_startsendswith()
{
	CPPUNIT_ASSERT_EQUAL(false, fz::starts_with(std::string("hello"), std::string("world")));
	CPPUNIT_ASSERT_EQUAL(true, fz::starts_with(std::string("hello"), std::string("hell")));
	CPPUNIT_ASSERT_EQUAL(false, fz::starts_with(std::string("hell"), std::string("hello")));
	CPPUNIT_ASSERT_EQUAL(false, fz::starts_with(std::string("hello"), std::string("HELL")));

	CPPUNIT_ASSERT_EQUAL(false, fz::starts_with<true>(std::string("hello"), std::string("world")));
	CPPUNIT_ASSERT_EQUAL(true, fz::starts_with<true>(std::string("hello"), std::string("hell")));
	CPPUNIT_ASSERT_EQUAL(false, fz::starts_with<true>(std::string("hell"), std::string("hello")));
	CPPUNIT_ASSERT_EQUAL(true, fz::starts_with<true>(std::string("hello"), std::string("HELL")));

	CPPUNIT_ASSERT_EQUAL(false, fz::ends_with(std::string("hello"), std::string("world")));
	CPPUNIT_ASSERT_EQUAL(true, fz::ends_with(std::string("hello"), std::string("ello")));
	CPPUNIT_ASSERT_EQUAL(false, fz::ends_with(std::string("ello"), std::string("HELLO")));
	CPPUNIT_ASSERT_EQUAL(false, fz::ends_with(std::string("hello"), std::string("ELLO")));

	CPPUNIT_ASSERT_EQUAL(false, fz::ends_with<true>(std::string("hello"), std::string("world")));
	CPPUNIT_ASSERT_EQUAL(true, fz::ends_with<true>(std::string("hello"), std::string("ello")));
	CPPUNIT_ASSERT_EQUAL(false, fz::ends_with<true>(std::string("ello"), std::string("HELLO")));
	CPPUNIT_ASSERT_EQUAL(true, fz::ends_with<true>(std::string("hello"), std::string("ELLO")));
}
