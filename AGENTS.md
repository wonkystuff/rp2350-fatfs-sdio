# Working Practices

This file captures the working conventions that have emerged on this project.
It is intended to help future work stay consistent even when the code changes
hands or the conversation context is gone.

The general workflow is reusable in other projects. When adopting it elsewhere,
review the project-specific paths, hardware assumptions, build commands and
release procedures rather than copying them unchanged. Keep detailed product
contracts in README.md and provenance in NOTICE.md. Keep this file focused on
working practices and the constraints agents must preserve.

## General approach

- Favor small, coherent refactors over large "cleanup blobs".
- Keep behavior unchanged when performing structural cleanup unless the change
  is explicitly intended to alter library behaviour or its public API.
- Treat the current product as a disciplined first iteration rather than a
  place to load every future idea.
- Start experiments on their own descriptive `experiment/` branch before changing
  code, rather than mixing experimental work into the current development line.
- After each refactor, audit the affected code and its callers for stale or
  duplicated code. Clean up findings within that change's scope, verify again,
  and record larger follow-ups in a TODO document.
- Optimize against an observed need, with before/after measurements where
  practical. Do not trade readability or stable behavior for speculative gains.

## Commits

- Prefer small commits with one clear purpose.
- Commit once a change is verified and conceptually tidy rather than waiting
  for a very large batch of work.
- When the user confirms a successful hardware test, commit the verified code
  and synced documentation before starting the next change; no separate commit
  request is needed. Leave unrelated local changes out of that commit.
- Think of each commit as a climbing piton: it limits how far we fall if the
  next move turns out to be wrong.
- If a refactor and a feature idea are unrelated, split them into separate
  commits.
- Use commit messages that describe the architectural or behavioral intent, not
  just the files touched.
- After a verified merge, audit branches against the repository's own main.
  Check ancestry and, where relevant, cherry-picked or superseded content before
  proposing cleanup. Delete only authorised branch references; keep unmerged
  work, release history and stashes unless their removal is explicitly agreed.
  Treat remote-branch deletion as a separate publication action, especially
  when local main contains commits that have not been pushed.

## Verification

- Add focused regression coverage with bug fixes. Reproduce the failure before
  fixing it where practical, then verify the intended behavior through existing
  public boundaries. If new coverage already passes, do not invent a production
  change merely to accompany the test.
- Report native tests, firmware compilation and hardware acceptance separately.
  State what was actually run and what remains unverified. Passing tests or
  observed timing maxima do not establish universal SD-card compatibility,
  durable writes, power-loss safety, proven WCET or absence of all races.
- For protocol/diskio changes, run the native tests; for filesystem/configuration
  changes, also run the optional real FatFs tests against the pinned revision.
  Build the Pico example for SDK, PIO, DMA or target integration changes.
- Test-only and documentation-only changes normally need no hardware acceptance
  unless they also alter firmware behaviour or reveal a runtime fix.
- For runtime-path changes, seek hardware acceptance where practical. Compilation
  or a request to edit code does not authorise flashing a device or writing to
  an SD card. Use an explicitly agreed device and backed-up test card; report
  outstanding hardware checks rather than implying they passed.
- If a bug is rare or timing-sensitive, prefer lightweight targeted telemetry
  over heavy continuous `printf` output.

Native protocol tests need no SDK, network or hardware:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

To include real FatFs coverage, use an existing checkout at the exact revision
pinned in CMakeLists.txt:

```sh
cmake -S tests -B build-tests -DFATFS_SOURCE_DIR=/path/to/pinned/fatfs
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

With PICO_SDK_PATH set to an installed SDK (CI currently uses 2.3.0):

```sh
cmake -S examples/list-directory -B build-example -DPICO_BOARD=pico2
cmake --build build-example
```

Run `reuse lint` for licensing/metadata changes and new files. The reproducible
sanitizer, clang-tidy, actionlint and CodeQL configurations live in
`.github/workflows/` and `.clang-tidy`; keep local verification aligned with them.
The ScanCode inventory is evidence for review, not a licence-compatibility verdict.

## Library contracts and repository boundary

- Preserve the single-card, single-driver, serialized-owner contract. The library
  does not provide concurrent access, locks or background hotplug ownership.
- Keep protocol operations synchronous and allocation-free. Blocking storage I/O
  belongs in the storage owner's context, outside interrupt/timer callbacks.
- FatFs remains read-only. Raw writes require exclusive ownership with the local
  filesystem unmounted; serializing calls cannot reconcile two filesystem owners.
- Preserve explicit partial-write failure handling: no automatic write retries,
  atomicity claims or implication that a later sync repairs a failed write.
- Read README.md for the detailed lifecycle, buffer, configuration and resource
  contracts before changing these paths. Deliberate contract changes need matching
  implementation, regression coverage and documentation.
- Build and test using this repository and its declared public dependencies and
  SDK/toolchain prerequisites. The sibling player application is a consumer,
  not an implicit build dependency. Keep consumer-specific policy out of the
  library; report consumer hardware acceptance separately when available.
- Keep FetchContent revisions pinned. Review source changes and licensing when
  updating them, and synchronize the component revisions in NOTICE.md.

## Documentation

- Sync docs and diagrams just before committing architectural changes.
- Keep the user-facing changelog current for features, public API changes
  and meaningful bug fixes. Purely internal refactors and test/documentation
  additions generally belong in commit messages and developer docs instead.
- Prefix user-facing changelog entries that correct bugs with `**[BUGFIX]**`
  so readers can distinguish fixes from new features. Keep unrelated fixes and
  features in separate entries; do not label new functionality as a bug fix.
- Preserve the user's wording/style and established document-generation workflow;
  do not silently replace it with another generator or rewrite unrelated prose.
- Keep the authoritative living docs to a small set: README.md for behaviour,
  build/test instructions and changelog; NOTICE.md for component provenance;
  LICENSE, LICENSES/ and third-party-notices/ for licence terms and notices.
- Avoid creating extra planning/review files unless they are genuinely needed.
- Fold enduring insights back into the living docs rather than scattering them
  across temporary notes.

## Releases and publishing

- Check the licences and bundled/derived-code provenance of new dependencies
  before incorporating them. Do not assume a top-level permissive licence covers
  every file; flag copyleft obligations or uncertainty before proceeding.
  Preserve source attribution and update NOTICE.md and REUSE metadata as needed.
- Local builds and commits do not by themselves authorise pushing branches/tags,
  publishing releases or uploading artifacts. Follow the user's publication scope.
- Record user-facing changes under README.md's Unreleased heading. On release,
  promote those entries to a version/date section (YYYY-MM-DD), then start a new
  Unreleased section. Keep published tags immutable.

## Code style

- Use C++ features when they improve clarity, not just for their own sake.
- Make code readable as a first priority: prefer names that describe a value's
  role and lifetime over names that reflect a past implementation detail.
- Prefer clear ownership boundaries and focused modules over "C with classes"
  or giant coordinator objects.
- Split classes and files at real functional or runtime ownership boundaries,
  not merely to make private helpers individually mockable or testable.
- Treat testability as a consequence of a coherent public boundary. Do not add
  forwarding layers, one-call interfaces, or duplicate state solely for tests.
- Add short intent-level comments where timing choices, cross-core handoffs, or
  ownership boundaries are non-obvious.
- When a class member is not self-evident, document whether it is canonical
  owned state, a cache of external data, a published snapshot, or a temporary
  work buffer. Avoid comments that merely restate the declaration.
- Keep low-level includes local to the module that actually uses them.

## Hardware and local tweaks

- Do not commit temporary local hardware experiment changes unless explicitly
  requested.
- When a hardware quirk is discovered, capture it in the docs or to-do list so
  it is not rediscovered later.

## Current project-specific habits

- Prefer small incremental commits.
- Sync docs before committing.
- Preserve a warm, collaborative, low-drama working style in code comments,
  docs, and commit structure: make progress easy to follow.
