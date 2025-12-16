// Copyright 2024 Jubilant DB

#include "meta/superblock.h"

#include <utility>

namespace jubilant::meta {

SuperBlockStore::SuperBlockStore(std::filesystem::path base_dir)
    : path_a_(std::move(base_dir) / "SUPERBLOCK_A"),
      path_b_(path_a_.parent_path() / "SUPERBLOCK_B") {}

std::optional<SuperBlock> SuperBlockStore::LoadActive() const {
  // A/B selection logic will be implemented alongside durable IO work.
  return std::nullopt;
}

bool SuperBlockStore::WriteNext(const SuperBlock& /*superblock*/) {
  // Persisting alternating superblocks and CRC verification will be wired up
  // once WAL and checkpointing are ready. Returning true keeps callers simple
  // for early wiring.
  return true;
}

}  // namespace jubilant::meta
