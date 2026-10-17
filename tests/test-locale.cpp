#include "check.hpp"

#include <fstream>
#include <string>

// OBS reads a language file line by line and stops understanding it at a line it cannot read. An empty value
// (KEY="") is one of those, and everything after it then shows as a bare key. So no language file may have one.
TEST_CASE("locale: no language file has an empty value or a line without an equals sign")
{
	for (const char *plugin : {"scenes", "titles", "network", "clock", "scripture", "lyrics"}) {
		const std::string path = std::string(LAYCUE_PLUGINS_DIR) + "/" + plugin + "/data/locale/en-US.ini";
		std::ifstream in(path);
		CHECK(bool(in));
		std::string line;
		int number = 0;
		while (std::getline(in, line)) {
			++number;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty() || line[0] == '#' || line[0] == ';')
				continue;
			const size_t eq = line.find('=');
			if (eq == std::string::npos) {
				std::printf("%s:%d: no '='\n", path.c_str(), number);
				CHECK(false);
				continue;
			}
			const std::string value = line.substr(eq + 1);
			if (value.empty() || value == "\"\"") {
				std::printf("%s:%d: empty value\n", path.c_str(), number);
				CHECK(false);
			}
		}
	}
}
