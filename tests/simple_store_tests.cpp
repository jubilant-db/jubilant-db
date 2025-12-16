#include <filesystem>
#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "storage/btree/btree.h"
#include "storage/simple_store.h"

using jubilant::storage::SimpleStore;
using jubilant::storage::btree::Record;

namespace fs = std::filesystem;

namespace {

fs::path TempDir(const std::string& name) {
  const auto dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  return dir;
}

}  // namespace

TEST(SimpleStoreTest, SetGetAndDelete) {
  const auto dir = TempDir("jubilant-simple-store-1");
  auto store = SimpleStore::Open(dir);

  Record record{};
  record.value = std::string{"value"};

  store.Set("key", record);
  const auto found = store.Get("key");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(std::get<std::string>(found->value), "value");

  EXPECT_TRUE(store.Delete("key"));
  EXPECT_FALSE(store.Get("key").has_value());
}

TEST(SimpleStoreTest, PersistsAcrossReopen) {
  const auto dir = TempDir("jubilant-simple-store-2");
  {
    auto store = SimpleStore::Open(dir);
    Record record{};
    record.value = std::int64_t{42};
    store.Set("answer", record);
    store.Sync();
  }

  auto reopened = SimpleStore::Open(dir);
  const auto found = reopened.Get("answer");
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(std::get<std::int64_t>(found->value), 42);
}
