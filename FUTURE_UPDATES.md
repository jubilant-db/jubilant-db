# Future updates roadmap (short list)

The recent validation and durability touch-ups surfaced a few clear next steps:

1. **Manifest durability hardening**
   * Add an fsync barrier after writing the manifest to guarantee the size-prefixed FlatBuffer hits disk before the first page or WAL segment is appended.
   * Track a monotonic manifest version so readers can detect and reject stale snapshots during concurrent init flows.

2. **SimpleStore persistence parity**
   * Compact multiple inline record pages into a single logical segment to avoid log growth from repeated overwrites.
   * Thread the manifest inline threshold into SimpleStore so large values are routed to the value log once persistence is wired up.

3. **Checkpoint + WAL integration**
   * Let `Checkpointer` surface the number of flushed pages and update the superblock with a durable checkpoint LSN.
   * Extend `WalManager::Flush` to write and fsync segment files, then surface the durability cursor to checkpoint scheduling.

4. **TTL observability**
   * Use the `TtlCalibration` fields to compute wall-clock expiration for records returned by `SimpleStore::Get`, returning metadata alongside values.
   * Add a maintenance command in `jubectl` to force-expire keys based on TTL drift calculations.

