// A tiny test helper so the project needs no test library.
//
//   TEST_CASE("adds numbers") { CHECK(1 + 1 == 2); CHECK_EQ(add(2, 2), 4); }
//
// Test cases register themselves; tests/test-main.cpp runs them all.
#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace check {

struct TestCase {
	std::string name;
	std::function<void()> body;
};

inline std::vector<TestCase> &registry()
{
	static std::vector<TestCase> cases;
	return cases;
}

inline int &failureCount()
{
	static int failures = 0;
	return failures;
}

struct Registrar {
	Registrar(const char *name, std::function<void()> body) { registry().push_back({name, std::move(body)}); }
};

inline void fail(const char *file, int line, const std::string &message)
{
	++failureCount();
	std::cerr << file << ":" << line << ": FAILED: " << message << "\n";
}

} // namespace check

#define CHECK_CONCAT_INNER(a, b) a##b
#define CHECK_CONCAT(a, b) CHECK_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                        \
	static void CHECK_CONCAT(checkTest_, __LINE__)();                                      \
	static const check::Registrar CHECK_CONCAT(checkRegistrar_, __LINE__)(                 \
		name, CHECK_CONCAT(checkTest_, __LINE__));                                     \
	static void CHECK_CONCAT(checkTest_, __LINE__)()

#define CHECK(expr)                                                     \
	do {                                                            \
		if (!(expr))                                            \
			check::fail(__FILE__, __LINE__, "CHECK(" #expr ")"); \
	} while (false)

#define CHECK_EQ(actual, expected)                                                                         \
	do {                                                                                               \
		const auto &checkActual = (actual);                                                        \
		const auto &checkExpected = (expected);                                                    \
		if (!(checkActual == checkExpected))                                                       \
			check::fail(__FILE__, __LINE__, "CHECK_EQ(" #actual ", " #expected ")");          \
	} while (false)
