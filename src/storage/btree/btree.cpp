#include "storage/btree/btree.h"

namespace jubilant::storage::btree {

auto BTree::Find(const std::string& key) const -> std::optional<Record> {
  auto in_memory_iter = in_memory_.find(key);
  if (in_memory_iter == in_memory_.end()) {
    return std::nullopt;
  }
  return in_memory_iter->second;
}

void BTree::Insert(const std::string& key, Record record) {
  in_memory_.insert_or_assign(key, std::move(record));
}

auto BTree::Erase(const std::string& key) -> bool { return in_memory_.erase(key) > 0; }

auto BTree::size() const noexcept -> std::size_t { return in_memory_.size(); }

}  // namespace jubilant::storage::btree
