# Ultra Tab agent instructions

Ultra Tab is the card deck beside lapis: `mac/` is the macOS overlay (Qt 6
Quick, C++20), `ios/` the iPhone app (SwiftUI), `scripts/` packaging and checks.
lapis is the pinned submodule `lapis/`; Ultra Tab builds against its session
transport, launch identity, Tab order and next-prompt helper, and reads only
the files and gateway routes lapis publishes (see README.md). Change lapis in
its own repository, then move the submodule.

- Build and test: `cmake --preset dev && cmake --build build/dev && ctest --preset dev`.
- Python: `uv run --no-project python -m unittest discover -s scripts/tests`.
- iPhone: `uv run --no-project python scripts/check_ultratab_ios.py` (headless simulator).
- Follow lapis's [code standards](https://github.com/RESMP-DEV/lapis/blob/main/CONTRIBUTING.md#code-standards).
  Never commit credentials, private transcripts, or machine identities (host
  names, addresses, account IDs, usernames in paths).
