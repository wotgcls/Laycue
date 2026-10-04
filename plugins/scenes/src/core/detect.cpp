#include "core/detect.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace laycue {

namespace {

constexpr int kWorkingWidth = 640; // detection runs on a copy about this wide

struct CbCr {
	int cb = 0;
	int cr = 0;
};

// Colour without brightness (BT.601), scaled by 256. Enough to compare greens.
CbCr toCbCr(int r, int g, int b)
{
	return {(-43 * r - 85 * g + 128 * b) / 256, (128 * r - 107 * g - 21 * b) / 256};
}

bool isStronglyGreen(int r, int g, int b)
{
	return g > r + 60 && g > b + 60;
}

// Decides, for one pixel, whether it belongs to the window.
class WindowTest {
public:
	WindowTest(const DetectOptions &options, const Rgb &key)
		: kind_(options.kind),
		  alphaThreshold_(options.alphaThreshold),
		  tolerance2_(options.chromaTolerance * options.chromaTolerance),
		  key_(toCbCr(key.r, key.g, key.b))
	{
	}

	bool operator()(const uint8_t *p) const
	{
		if (kind_ == WindowKind::SeeThrough)
			return p[3] < alphaThreshold_;
		if (p[3] < alphaThreshold_)
			return false; // already see-through: not part of a green window
		const CbCr c = toCbCr(p[0], p[1], p[2]);
		const int dcb = c.cb - key_.cb;
		const int dcr = c.cr - key_.cr;
		return dcb * dcb + dcr * dcr < tolerance2_;
	}

private:
	WindowKind kind_;
	int alphaThreshold_;
	int tolerance2_;
	CbCr key_;
};

// Window / not-window for each cell of a smaller grid (each cell = scale × scale pixels).
struct Mask {
	int width = 0;
	int height = 0;
	int scale = 1;
	std::vector<uint8_t> cells;
};

Mask buildMask(const ImageView &image, const WindowTest &isWindow)
{
	Mask mask;
	mask.scale = std::max(1, (image.width + kWorkingWidth - 1) / kWorkingWidth);
	mask.width = (image.width + mask.scale - 1) / mask.scale;
	mask.height = (image.height + mask.scale - 1) / mask.scale;
	mask.cells.assign(static_cast<size_t>(mask.width) * mask.height, 0);

	for (int cy = 0; cy < mask.height; ++cy) {
		const int y0 = cy * mask.scale;
		const int y1 = std::min(y0 + mask.scale, image.height);
		for (int cx = 0; cx < mask.width; ++cx) {
			const int x0 = cx * mask.scale;
			const int x1 = std::min(x0 + mask.scale, image.width);
			int hits = 0;
			for (int y = y0; y < y1; ++y) {
				for (int x = x0; x < x1; ++x)
					hits += isWindow(image.at(x, y)) ? 1 : 0;
			}
			// A cell is window when at least half its pixels are.
			mask.cells[static_cast<size_t>(cy) * mask.width + cx] = 2 * hits >= (y1 - y0) * (x1 - x0) ? 1
														  : 0;
		}
	}
	return mask;
}

struct Region {
	long cells = 0;
	int minX = 0;
	int minY = 0;
	int maxX = -1;
	int maxY = -1;
};

// The largest 4-connected area of window cells.
Region largestRegion(const Mask &mask)
{
	std::vector<uint8_t> seen(mask.cells.size(), 0);
	std::vector<int> stack;
	Region best;

	for (int start = 0; start < static_cast<int>(mask.cells.size()); ++start) {
		if (!mask.cells[start] || seen[start])
			continue;
		Region region;
		region.minX = mask.width;
		region.minY = mask.height;
		seen[start] = 1;
		stack.push_back(start);
		while (!stack.empty()) {
			const int index = stack.back();
			stack.pop_back();
			const int x = index % mask.width;
			const int y = index / mask.width;
			++region.cells;
			region.minX = std::min(region.minX, x);
			region.maxX = std::max(region.maxX, x);
			region.minY = std::min(region.minY, y);
			region.maxY = std::max(region.maxY, y);

			const std::array<std::array<int, 2>, 4> neighbours{
				{{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}}};
			for (const auto &n : neighbours) {
				if (n[0] < 0 || n[1] < 0 || n[0] >= mask.width || n[1] >= mask.height)
					continue;
				const int next = n[1] * mask.width + n[0];
				if (mask.cells[next] && !seen[next]) {
					seen[next] = 1;
					stack.push_back(next);
				}
			}
		}
		if (region.cells > best.cells)
			best = region;
	}
	return best;
}

// Share of window pixels along one column (x fixed, y in [y0, y1)) or one row.
double columnShare(const ImageView &image, const WindowTest &isWindow, int x, int y0, int y1)
{
	int hits = 0;
	for (int y = y0; y < y1; ++y)
		hits += isWindow(image.at(x, y)) ? 1 : 0;
	return y1 > y0 ? static_cast<double>(hits) / (y1 - y0) : 0;
}

double rowShare(const ImageView &image, const WindowTest &isWindow, int y, int x0, int x1)
{
	int hits = 0;
	for (int x = x0; x < x1; ++x)
		hits += isWindow(image.at(x, y)) ? 1 : 0;
	return x1 > x0 ? static_cast<double>(hits) / (x1 - x0) : 0;
}

// Moves each edge of the rough rectangle (edges exclusive at x1/y1) to the exact pixel
// where the window starts, searching a few pixels either side. Measured along the middle
// 80% of the edge so rounded or decorated corners don't pull it.
void refineEdges(const ImageView &image, const WindowTest &isWindow, int reach, int &x0, int &y0, int &x1, int &y1)
{
	const auto middle = [](int a, int b, int &from, int &to) {
		const int inset = (b - a) / 10;
		from = a + inset;
		to = std::max(from + 1, b - inset);
	};

	int from = 0;
	int to = 0;
	middle(y0, y1, from, to);
	for (int x = std::max(0, x0 - reach); x <= std::min(image.width - 1, x0 + reach); ++x) {
		if (columnShare(image, isWindow, x, from, to) >= 0.5) {
			x0 = x;
			break;
		}
	}
	for (int x = std::min(image.width - 1, x1 - 1 + reach); x >= std::max(0, x1 - 1 - reach); --x) {
		if (columnShare(image, isWindow, x, from, to) >= 0.5) {
			x1 = x + 1;
			break;
		}
	}

	middle(x0, x1, from, to);
	for (int y = std::max(0, y0 - reach); y <= std::min(image.height - 1, y0 + reach); ++y) {
		if (rowShare(image, isWindow, y, from, to) >= 0.5) {
			y0 = y;
			break;
		}
	}
	for (int y = std::min(image.height - 1, y1 - 1 + reach); y >= std::max(0, y1 - 1 - reach); --y) {
		if (rowShare(image, isWindow, y, from, to) >= 0.5) {
			y1 = y + 1;
			break;
		}
	}
}

} // namespace

std::optional<Rgb> sampleKeyColour(const ImageView &image)
{
	if (!image.pixels || image.width <= 0 || image.height <= 0)
		return std::nullopt;

	// Count strongly green pixels in buckets of similar colour (5 bits per channel).
	std::vector<uint32_t> counts(32 * 32 * 32, 0);
	for (int y = 0; y < image.height; ++y) {
		for (int x = 0; x < image.width; ++x) {
			const uint8_t *p = image.at(x, y);
			if (p[3] < 128 || !isStronglyGreen(p[0], p[1], p[2]))
				continue;
			++counts[(p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)];
		}
	}
	const auto top = std::max_element(counts.begin(), counts.end());
	if (*top == 0)
		return std::nullopt;
	const int bucket = static_cast<int>(top - counts.begin());

	// The average colour of the pixels in the most common bucket.
	uint64_t r = 0, g = 0, b = 0, n = 0;
	for (int y = 0; y < image.height; ++y) {
		for (int x = 0; x < image.width; ++x) {
			const uint8_t *p = image.at(x, y);
			if (p[3] < 128 || !isStronglyGreen(p[0], p[1], p[2]))
				continue;
			if (((p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)) != bucket)
				continue;
			r += p[0];
			g += p[1];
			b += p[2];
			++n;
		}
	}
	return Rgb{static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n)};
}

DetectResult detectWindow(const ImageView &image, const DetectOptions &options)
{
	DetectResult result;
	result.kind = options.kind;
	if (!image.pixels || image.width <= 0 || image.height <= 0)
		return result;

	if (options.kind == WindowKind::Green) {
		const std::optional<Rgb> key = options.key ? options.key : sampleKeyColour(image);
		if (!key)
			return result; // no green in the image
		result.key = *key;
	}

	const WindowTest isWindow(options, result.key);
	const Mask mask = buildMask(image, isWindow);
	const Region region = largestRegion(mask);
	const double share = static_cast<double>(region.cells) * mask.scale * mask.scale /
			     (static_cast<double>(image.width) * image.height);
	if (region.cells == 0 || share < kMinWindowShare)
		return result;

	const long boxCells = static_cast<long>(region.maxX - region.minX + 1) * (region.maxY - region.minY + 1);
	result.fillRatio = static_cast<double>(region.cells) / static_cast<double>(boxCells);

	int x0 = region.minX * mask.scale;
	int y0 = region.minY * mask.scale;
	int x1 = std::min((region.maxX + 1) * mask.scale, image.width);
	int y1 = std::min((region.maxY + 1) * mask.scale, image.height);
	refineEdges(image, isWindow, 2 * mask.scale, x0, y0, x1, y1);

	result.found = x1 > x0 && y1 > y0;
	result.window = {x0, y0, x1 - x0, y1 - y0};
	result.lowConfidence = result.fillRatio < kConfidentFill;
	return result;
}

DetectResult detectWindowAuto(const ImageView &image)
{
	DetectOptions options;
	options.kind = WindowKind::SeeThrough;
	const DetectResult seeThrough = detectWindow(image, options);
	if (seeThrough.found)
		return seeThrough;

	options.kind = WindowKind::Green;
	const DetectResult green = detectWindow(image, options);
	return green.found ? green : seeThrough;
}

} // namespace laycue
