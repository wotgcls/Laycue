#include "core/scene-model.hpp"

namespace laycue {

size_t countItems(const std::vector<ItemInfo> &items)
{
	size_t total = 0;
	for (const ItemInfo &item : items)
		total += 1 + countItems(item.children);
	return total;
}

} // namespace laycue
