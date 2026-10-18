#include "core/qr.hpp"

#include <algorithm>
#include <cstdlib>

namespace laycue::titles {

namespace {

constexpr int kMaxVersion = 10;

// Index: level (0 = L, 1 = M), then version 1..10. Check bytes per block, and number of blocks.
constexpr int kEccPerBlock[2][kMaxVersion + 1] = {
	{-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
	{-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
};
constexpr int kBlocks[2][kMaxVersion + 1] = {
	{-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
	{-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
};
constexpr int kFormatBits[2] = {1, 0}; // L = 01, M = 00

int rawDataModules(int ver)
{
	int result = (16 * ver + 128) * ver + 64;
	if (ver >= 2) {
		const int align = ver / 7 + 2;
		result -= (25 * align - 10) * align - 55;
		if (ver >= 7)
			result -= 36;
	}
	return result;
}

int dataCodewords(int ver, int level)
{
	return rawDataModules(ver) / 8 - kEccPerBlock[level][ver] * kBlocks[level][ver];
}

bool bitOf(uint32_t value, int i)
{
	return ((value >> i) & 1U) != 0;
}

// Arithmetic in GF(256) with the QR polynomial 0x11D.
uint8_t gfMultiply(uint8_t x, uint8_t y)
{
	int z = 0;
	for (int i = 7; i >= 0; --i) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	return uint8_t(z);
}

std::vector<uint8_t> reedSolomonDivisor(int degree)
{
	std::vector<uint8_t> result(size_t(degree), 0);
	result[size_t(degree) - 1] = 1;
	uint8_t root = 1;
	for (int i = 0; i < degree; ++i) {
		for (size_t j = 0; j < result.size(); ++j) {
			result[j] = gfMultiply(result[j], root);
			if (j + 1 < result.size())
				result[j] ^= result[j + 1];
		}
		root = gfMultiply(root, 0x02);
	}
	return result;
}

std::vector<uint8_t> reedSolomonRemainder(const std::vector<uint8_t> &data, const std::vector<uint8_t> &divisor)
{
	std::vector<uint8_t> result(divisor.size(), 0);
	for (uint8_t b : data) {
		const uint8_t factor = uint8_t(b ^ result.front());
		result.erase(result.begin());
		result.push_back(0);
		for (size_t i = 0; i < result.size(); ++i)
			result[i] ^= gfMultiply(divisor[i], factor);
	}
	return result;
}

// The grid while it is being built: which squares are dark, and which belong to the fixed patterns.
struct Grid {
	int size;
	std::vector<uint8_t> dark;
	std::vector<uint8_t> fixed;
	explicit Grid(int s) : size(s), dark(size_t(s) * size_t(s), 0), fixed(size_t(s) * size_t(s), 0) {}
	size_t at(int x, int y) const { return size_t(y) * size_t(size) + size_t(x); }
	void setFixed(int x, int y, bool d)
	{
		dark[at(x, y)] = d ? 1 : 0;
		fixed[at(x, y)] = 1;
	}
};

void drawFinder(Grid &g, int cx, int cy)
{
	for (int dy = -4; dy <= 4; ++dy) {
		for (int dx = -4; dx <= 4; ++dx) {
			const int dist = std::max(std::abs(dx), std::abs(dy));
			const int x = cx + dx, y = cy + dy;
			if (x >= 0 && x < g.size && y >= 0 && y < g.size)
				g.setFixed(x, y, dist != 2 && dist != 4);
		}
	}
}

void drawAlignment(Grid &g, int cx, int cy)
{
	for (int dy = -2; dy <= 2; ++dy)
		for (int dx = -2; dx <= 2; ++dx)
			g.setFixed(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

std::vector<int> alignmentPositions(int ver, int size)
{
	std::vector<int> result;
	if (ver == 1)
		return result;
	const int count = ver / 7 + 2;
	const int step = (ver * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
	for (int i = 0, pos = size - 7; i < count - 1; ++i, pos -= step)
		result.insert(result.begin(), pos);
	result.insert(result.begin(), 6);
	return result;
}

void drawFormat(Grid &g, int level, int mask)
{
	const uint32_t data = uint32_t(kFormatBits[level] << 3 | mask);
	uint32_t rem = data;
	for (int i = 0; i < 10; ++i)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	const uint32_t bits = ((data << 10) | rem) ^ 0x5412;
	for (int i = 0; i <= 5; ++i)
		g.setFixed(8, i, bitOf(bits, i));
	g.setFixed(8, 7, bitOf(bits, 6));
	g.setFixed(8, 8, bitOf(bits, 7));
	g.setFixed(7, 8, bitOf(bits, 8));
	for (int i = 9; i < 15; ++i)
		g.setFixed(14 - i, 8, bitOf(bits, i));
	for (int i = 0; i < 8; ++i)
		g.setFixed(g.size - 1 - i, 8, bitOf(bits, i));
	for (int i = 8; i < 15; ++i)
		g.setFixed(8, g.size - 15 + i, bitOf(bits, i));
	g.setFixed(8, g.size - 8, true); // the one square that is always dark
}

void drawVersion(Grid &g, int ver)
{
	if (ver < 7)
		return;
	uint32_t rem = uint32_t(ver);
	for (int i = 0; i < 12; ++i)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	const uint32_t bits = (uint32_t(ver) << 12) | rem;
	for (int i = 0; i < 18; ++i) {
		const bool bit = bitOf(bits, i);
		const int a = g.size - 11 + i % 3, b = i / 3;
		g.setFixed(a, b, bit);
		g.setFixed(b, a, bit);
	}
}

void drawFixedPatterns(Grid &g, int ver)
{
	for (int i = 0; i < g.size; ++i) {
		g.setFixed(6, i, i % 2 == 0);
		g.setFixed(i, 6, i % 2 == 0);
	}
	drawFinder(g, 3, 3);
	drawFinder(g, g.size - 4, 3);
	drawFinder(g, 3, g.size - 4);
	const std::vector<int> pos = alignmentPositions(ver, g.size);
	const size_t n = pos.size();
	for (size_t i = 0; i < n; ++i) {
		for (size_t j = 0; j < n; ++j) {
			if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
				continue;
			drawAlignment(g, pos[i], pos[j]);
		}
	}
	drawFormat(g, 0, 0); // so the squares count as fixed; drawn again with the real level and mask
	drawVersion(g, ver);
}

void placeCodewords(Grid &g, const std::vector<uint8_t> &data)
{
	size_t i = 0;
	for (int right = g.size - 1; right >= 1; right -= 2) {
		if (right == 6)
			right = 5;
		for (int vert = 0; vert < g.size; ++vert) {
			for (int j = 0; j < 2; ++j) {
				const int x = right - j;
				const bool upward = ((right + 1) & 2) == 0;
				const int y = upward ? g.size - 1 - vert : vert;
				if (!g.fixed[g.at(x, y)] && i < data.size() * 8) {
					g.dark[g.at(x, y)] = bitOf(data[i >> 3], 7 - int(i & 7)) ? 1 : 0;
					++i;
				}
			}
		}
	}
}

void applyMask(Grid &g, int mask)
{
	for (int y = 0; y < g.size; ++y) {
		for (int x = 0; x < g.size; ++x) {
			bool invert = false;
			switch (mask) {
			case 0:
				invert = (x + y) % 2 == 0;
				break;
			case 1:
				invert = y % 2 == 0;
				break;
			case 2:
				invert = x % 3 == 0;
				break;
			case 3:
				invert = (x + y) % 3 == 0;
				break;
			case 4:
				invert = (x / 3 + y / 2) % 2 == 0;
				break;
			case 5:
				invert = x * y % 2 + x * y % 3 == 0;
				break;
			case 6:
				invert = (x * y % 2 + x * y % 3) % 2 == 0;
				break;
			default:
				invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
				break;
			}
			if (invert && !g.fixed[g.at(x, y)])
				g.dark[g.at(x, y)] ^= 1;
		}
	}
}

// How hard a masked code is to scan: runs of one colour, 2x2 blocks, finder-like patterns, uneven balance.
int penalty(const Grid &g)
{
	const int n = g.size;
	int result = 0;
	auto d = [&](int x, int y) { return g.dark[g.at(x, y)] != 0; };
	for (int pass = 0; pass < 2; ++pass) {
		for (int a = 0; a < n; ++a) {
			int run = 1;
			uint32_t bits = 0;
			for (int b = 0; b < n; ++b) {
				const bool cur = pass == 0 ? d(b, a) : d(a, b);
				if (b > 0) {
					const bool prev = pass == 0 ? d(b - 1, a) : d(a, b - 1);
					if (cur == prev) {
						++run;
						if (run == 5)
							result += 3;
						else if (run > 5)
							result += 1;
					} else {
						run = 1;
					}
				}
				bits = ((bits << 1) | (cur ? 1U : 0U)) & 0x7FF;
				if (b >= 10 && (bits == 0x05D || bits == 0x5D0))
					result += 40;
			}
		}
	}
	for (int y = 0; y + 1 < n; ++y) {
		for (int x = 0; x + 1 < n; ++x) {
			const bool c = d(x, y);
			if (c == d(x + 1, y) && c == d(x, y + 1) && c == d(x + 1, y + 1))
				result += 3;
		}
	}
	int dark = 0;
	for (uint8_t m : g.dark)
		dark += m;
	const int total = n * n;
	const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
	result += std::max(k, 0) * 10;
	return result;
}

} // namespace

QrCode encodeQr(const std::string &text, int forcedMask)
{
	QrCode none;
	if (text.empty())
		return none;
	// The smallest version that holds the text: level M first, then level L for the longest links.
	int level = -1, ver = 0;
	for (int lv = 1; lv >= 0 && level < 0; --lv) {
		for (int v = 1; v <= kMaxVersion; ++v) {
			const int countBits = v >= 10 ? 16 : 8;
			const int needed = 4 + countBits + 8 * int(text.size());
			if (needed <= dataCodewords(v, lv) * 8) {
				level = lv;
				ver = v;
				break;
			}
		}
	}
	if (level < 0)
		return none;

	// The data: mode (byte), length, the bytes, then the end mark and padding.
	std::vector<bool> bits;
	auto append = [&bits](uint32_t value, int count) {
		for (int i = count - 1; i >= 0; --i)
			bits.push_back(bitOf(value, i));
	};
	append(0x4, 4);
	append(uint32_t(text.size()), ver >= 10 ? 16 : 8);
	for (unsigned char c : text)
		append(c, 8);
	const size_t capacityBits = size_t(dataCodewords(ver, level)) * 8;
	for (int i = 0; i < 4 && bits.size() < capacityBits; ++i)
		bits.push_back(false);
	while (bits.size() % 8 != 0)
		bits.push_back(false);
	std::vector<uint8_t> data(bits.size() / 8, 0);
	for (size_t i = 0; i < bits.size(); ++i)
		data[i >> 3] = uint8_t(data[i >> 3] | (bits[i] ? 1U << (7 - (i & 7)) : 0U));
	for (uint8_t pad = 0xEC; data.size() < size_t(dataCodewords(ver, level)); pad ^= 0xEC ^ 0x11)
		data.push_back(pad);

	// Error correction: split into blocks, add each block's check bytes, then interleave the blocks.
	const int numBlocks = kBlocks[level][ver], eccLen = kEccPerBlock[level][ver];
	const int rawCodewords = rawDataModules(ver) / 8;
	const int numShort = numBlocks - rawCodewords % numBlocks;
	const int shortLen = rawCodewords / numBlocks;
	std::vector<std::vector<uint8_t>> blocks;
	const std::vector<uint8_t> divisor = reedSolomonDivisor(eccLen);
	for (int i = 0, k = 0; i < numBlocks; ++i) {
		const int len = shortLen - eccLen + (i < numShort ? 0 : 1);
		std::vector<uint8_t> block(data.begin() + k, data.begin() + k + len);
		k += len;
		const std::vector<uint8_t> ecc = reedSolomonRemainder(block, divisor);
		if (i < numShort)
			block.push_back(0); // a short block gets a spare slot so every block has the same length
		block.insert(block.end(), ecc.begin(), ecc.end());
		blocks.push_back(block);
	}
	std::vector<uint8_t> all;
	for (size_t i = 0; i < blocks[0].size(); ++i) {
		for (size_t j = 0; j < blocks.size(); ++j) {
			if (i != size_t(shortLen - eccLen) || j >= size_t(numShort))
				all.push_back(blocks[j][i]);
		}
	}

	const int size = ver * 4 + 17;
	Grid g(size);
	drawFixedPatterns(g, ver);
	placeCodewords(g, all);

	int bestMask = forcedMask;
	if (bestMask < 0 || bestMask > 7) {
		int bestPenalty = 1 << 30;
		for (int m = 0; m < 8; ++m) {
			applyMask(g, m);
			drawFormat(g, level, m);
			const int p = penalty(g);
			if (p < bestPenalty) {
				bestPenalty = p;
				bestMask = m;
			}
			applyMask(g, m); // masking twice undoes it
		}
	}
	applyMask(g, bestMask);
	drawFormat(g, level, bestMask);

	QrCode out;
	out.size = size;
	out.modules = g.dark;
	return out;
}

} // namespace laycue::titles
