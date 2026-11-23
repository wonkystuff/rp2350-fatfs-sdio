# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 wonkystuff

This repository pins its two CMake `FetchContent` dependencies to full commit
SHAs in `CMakeLists.txt`. The **Update pinned dependencies** workflow checks the
upstream default branches weekly and can also be started manually from the
Actions tab. If either pin has moved, it opens or updates a pull request with
the new exact SHAs; it does not merge the change, change the changelog, tag a
release, or publish a release.

Review each proposed upstream revision, including source changes and licence
notices/provenance, before merging. The pull request runs the normal CI checks.
Update `NOTICE.md` and the changelog as appropriate as part of the reviewed
change. Make a release only after the reviewed update is merged and accepted.

The updater only changes `GIT_TAG` values already expressed as full 40-character
commit SHAs. It does not import upstream code into this repository.
