// A small QR code encoder: a link goes in, a grid of dark and light squares comes out. Pure C++17, tested.
// Byte mode, error correction level M (level L when the text is too long for M), versions 1 to 10, which holds
// links of up to about 270 characters.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace laycue::titles {

struct QrCode {
	int size = 0;                 // squares along one side (21 for version 1 ... 57 for version 10)
	std::vector<uint8_t> modules; // size * size, row by row; 1 = dark
	bool empty() const { return size == 0; }
	bool dark(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < size && y < size && modules[size_t(y) * size_t(size) + size_t(x)] != 0;
	}
};

// An empty code when the text is empty or too long. `mask` is 0 to 7 to force a mask (tests); -1 picks the best.
QrCode encodeQr(const std::string &text, int mask = -1);

} // namespace laycue::titles
