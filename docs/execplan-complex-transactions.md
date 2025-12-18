# Integrate conditional multi-operation transactions with concurrency validation

This ExecPlan is a living document. The sections `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` must be kept up to date as work proceeds. Maintain this file in accordance with `PLANS.md` at the repository root.

## Purpose / Big Picture

Enable users to run multi-key transactions that mix reads, conditional assertions, and writes in a single request while preserving strict serializability under load. The end result should let a client issue a transaction containing predeclared read/write sets and ASSERT-style conditionals (e.g., assert a key exists before overwriting) and observe that concurrent workers execute them quickly without violating isolation. The capability will be demonstrated through targeted concurrency tests that fail today and pass after the implementation.

## Progress

- [x] (2025-12-18 10:52:38Z) Drafted initial ExecPlan based on current transaction pipeline, lock manager, and specification.
- [ ] (2025-12-18 10:52:38Z) Extend transaction model to express key tables, read/write modes, and ASSERT operations in request parsing and validation.
- [ ] (2025-12-18 10:52:38Z) Rework transaction execution to lock declared keys in canonical order, honor overlay read-your-writes, and enforce conditional checks before mutations.
- [ ] (2025-12-18 10:52:38Z) Add concurrency-focused unit and integration tests that prove correctness (no isolation violations) and exercise overlapping transactions for performance characteristics.
- [ ] (2025-12-18 10:52:38Z) Document the new transaction semantics in README/docs and adjust examples for conditional operations.
- [ ] (2025-12-18 10:52:38Z) Finalize retrospective after tests pass and concurrency guarantees are demonstrated.

## Surprises & Discoveries

- Observation: Current worker processing locks per operation and does not predeclare key sets, so canonical ordering for deadlock avoidance and lock sharing is absent. Evidence: `Worker::Process` iterates operations and locks inside each Apply* call in `src/server/worker.cpp`.
- Observation: Overlay support exists but reads inside workers bypass the overlay before hitting storage, meaning read-your-writes is only captured opportunistically after storage writes. Evidence: `TransactionContext::Read` only checks the overlay, while `Worker::ApplyRead` queries the BTree directly and writes into the overlay afterward.

## Decision Log

- Decision: Implement conditional operations using the ASSERT primitives specified in `MAIN_SPECIFICATION.md` §2.5 (ASSERT_EXISTS, ASSERT_NOT_EXISTS, ASSERT_TYPE, ASSERT_INT_EQ, ASSERT_BYTES_HASH_EQ, ASSERT_STRING_HASH_EQ) to avoid inventing new semantics and keep wire/runtime aligned with the published contract. Rationale: Aligns with the spec and keeps future FlatBuffers schema migration straightforward. Date/Author: 2025-12-18 / assistant.

## Outcomes & Retrospective

Pending implementation; this section will summarize concurrency proof points, gaps, and follow-ups after the test suite lands.

## Context and Orientation

Transactions are represented by `txn::TransactionRequest` (limited to `kGet`, `kSet`, `kDelete`) in `src/txn/transaction_request.*`, with `TransactionContext` providing a per-transaction overlay and state tracking in `src/txn/transaction_context.*`. The server pipeline uses `server::TransactionReceiver` as a bounded queue and `server::Worker` to process requests in threads (`src/server/worker.*`), acquiring locks per operation via `lock::LockManager` (`src/lock/lock_manager.*`) and directly mutating the BTree (`src/storage/btree/btree.*`). Current tests cover single-worker processing and basic queue behavior in `tests/server_worker_tests.cpp` and overlay state in `tests/transaction_context_tests.cpp`. There is no explicit key-table declaration, no ASSERT operations, and no test that drives concurrent conflicting transactions to prove serializability.

## Plan of Work

Begin by extending the transaction request model to include a key table with per-key modes (read or read-write) and an expanded operation enum covering ASSERT variants. Update validation to reject missing declarations, mismatched modes, or unsupported conditionals. Next, refactor the worker execution path to construct a transaction view: consult the overlay before storage reads, apply conditionals against the overlay+storage view, and batch lock acquisition by sorting declared keys to enforce canonical ordering before executing operations. Ensure writes stage in the overlay and commit to the BTree only after all conditionals pass, integrating with existing lock manager semantics and preparing hooks for WAL in future steps. Enhance the client/server framing (JSON transaction shape for now) to accept the richer request structure, keeping compatibility with the preview wire doc where possible.

Introduce unit tests that exercise conditional logic locally (e.g., ASSERT_NOT_EXISTS followed by SET, ASSERT_INT_EQ guarding an increment) and integration-style tests that spin up multiple workers sharing a `TransactionReceiver` to submit overlapping transactions. Design concurrency tests to include a mix of conflicting and non-conflicting key sets to show parallel execution (non-overlapping keys complete concurrently) while conflicting transactions serialize according to lock ordering without deadlocks or lost updates. Add timing/throughput assertions where practical (e.g., measuring that non-conflicting transactions complete within a bounded window compared to sequential execution). Update docs and examples to illustrate how to craft conditional transactions and interpret results.

## Concrete Steps

1. Expand transaction definitions in `src/txn/transaction_request.h/.cpp` to add key-table structures, ASSERT operation variants, and stricter validation (unknown key IDs, mode mismatches, missing values). Reflect these changes in any request builders or parsers used by the server.
2. Augment `src/txn/transaction_context.*` to maintain both overlay writes and cached reads, exposing helpers for ASSERT evaluation and a way to stage commit-ready mutations separate from runtime state transitions.
3. Refactor `src/server/worker.*` to:
   - Pre-collect declared keys from the request, sort them, and acquire shared/exclusive locks before executing operations.
   - Evaluate ASSERT operations against the overlay-first view, aborting the transaction without partial commits on failure.
   - Apply GET/SET/DELETE using the overlay to provide read-your-writes and defer durable BTree updates until after all checks succeed.
4. Add concurrency-aware tests:
   - Unit tests for ASSERT evaluation and overlay behavior in `tests/transaction_context_tests.cpp` or a new `tests/transaction_planner_tests.cpp`.
   - Multi-worker tests in `tests/server_worker_tests.cpp` (or a dedicated integration test) that enqueue interleaved transactions with overlapping keys to verify serialization and with disjoint keys to verify parallelism and performance.
5. Update docs: extend `docs/txn-wire-v0.0.2.md` and `README.md` examples to describe conditional operations and the single-frame transaction structure with key tables and declared modes.
6. Run tooling: `cmake --preset dev-debug`, `cmake --build --preset dev-debug --target clang-format`, `cmake --preset dev-debug-tidy`, `cmake --build --preset dev-debug-tidy`, then `ctest --preset dev-debug` (and focused `-R` filters for new tests) to confirm correctness and lints.

## Validation and Acceptance

Acceptance requires reproducible evidence that conditional transactions enforce correctness and remain fast under concurrency:

- New unit tests covering ASSERT_* behaviors and overlay read-your-writes fail before the implementation and pass afterward.
- Multi-worker tests demonstrate serializable outcomes: conflicting transactions serialize without deadlocks or lost updates, and disjoint key sets complete faster than a forced-sequential baseline (e.g., measured via timing assertions or completion counts within a time budget).
- End-to-end transaction examples in docs show how to send a transaction mixing ASSERT and SET/GET/DELETE, and the server responds with per-operation success flags matching the described semantics.

## Idempotence and Recovery

Code and tests should be safe to re-run: transaction planners should handle repeated key declarations deterministically, lock acquisition must use sorted keys to avoid deadlocks on retries, and tests should clean up temporary directories (`std::filesystem::remove_all`) before recreating storage structures. If a test allocates threads or receivers, ensure they are stopped/joined even on assertion failures by using RAII guards or `ASSERT_NO_FATAL_FAILURE` patterns.

## Artifacts and Notes

Capture key evidence such as:

- Snippets of transaction request JSON demonstrating key tables and ASSERT operations.
- Test output showing concurrent transaction completions and assertions about ordering/timing.
- Any decision changes captured in the `Decision Log` with rationale and date.

## Interfaces and Dependencies

Define the following interfaces to keep the implementation stable:

- Transaction request model additions in `src/txn/transaction_request.h`:
    struct KeySpec { uint32_t id; LockMode mode; std::string key; };
    enum class OperationType { kGet, kSet, kDelete, kAssertExists, kAssertNotExists, kAssertType, kAssertIntEq, kAssertBytesHashEq, kAssertStringHashEq };
    struct Operation { OperationType type; uint32_t key_id; std::optional<storage::btree::Record> value; std::optional<ExpectedTypeOrHash> expected; };
- Worker executor expectations in `src/server/worker.*`:
    Use sorted `KeySpec` entries to acquire locks before executing operations; expose a helper to evaluate ASSERT operations against a `TransactionContext` that merges overlay and BTree reads; ensure failures set transaction state to aborted and release locks cleanly.
- Testing utilities:
    Helper functions to build transaction requests with key tables and operation lists, plus timing harnesses to submit batches through `TransactionReceiver` to multiple `Worker` instances for concurrency validation.
