#include "check.hpp"

int main()
{
	for (const check::TestCase &test : check::registry()) {
		const int failuresBefore = check::failureCount();
		test.body();
		const bool passed = check::failureCount() == failuresBefore;
		std::cout << (passed ? "  ok    " : "  FAIL  ") << test.name << "\n";
	}

	const int failures = check::failureCount();
	std::cout << "\n" << check::registry().size() << " test cases, " << failures << " failed checks\n";
	return failures == 0 ? 0 : 1;
}
