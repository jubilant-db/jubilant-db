// Copyright 2024 Jubilant DB

#include "meta/manifest.h"

#include <utility>

namespace jubilant::meta {

ManifestStore::ManifestStore(std::filesystem::path base_dir)
    : manifest_path_(std::move(base_dir) / "MANIFEST") {}

ManifestRecord ManifestStore::NewDefault(std::string uuid_seed) const {
  ManifestRecord manifest{};
  manifest.db_uuid = std::move(uuid_seed);
  manifest.wire_schema = "wire-v1";
  manifest.disk_schema = "disk-v1";
  manifest.wal_schema = "wal-v1";
  return manifest;
}

std::optional<ManifestRecord> ManifestStore::Load() const {
  // Disk IO is intentionally deferred; callers can inject synthetic manifests
  // during TDD without touching the filesystem.
  return std::nullopt;
}

ManifestValidationResult ManifestStore::Validate(
    const ManifestRecord& manifest) const {
  ManifestValidationResult result{};
  result.ok = true;

  if (manifest.format_major == 0) {
    result.ok = false;
    result.message = "format_major must be non-zero";
  } else if (manifest.page_size == 0) {
    result.ok = false;
    result.message = "page_size must be non-zero";
  } else if (manifest.db_uuid.empty()) {
    result.ok = false;
    result.message = "db_uuid must be populated";
  }

  return result;
}

bool ManifestStore::Persist(const ManifestRecord& /*manifest*/) {
  // Persisting to disk will be added alongside FlatBuffers wiring. Returning
  // true keeps the call sites simple for now while allowing tests to assert on
  // validation and planning behavior.
  return true;
}

}  // namespace jubilant::meta
