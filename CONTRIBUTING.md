# Contributing

Thank you for helping improve Windows Hello Refined.

## Before you start

- Read the [README](README.md), [security policy](SECURITY.md), and [code of conduct](CODE_OF_CONDUCT.md).
- Do not include passwords, PINs, face images, embeddings, camera recordings, or other personal data in issues or pull requests.
- Keep changes focused and preserve existing Windows lock-screen behavior unless the change explicitly requires otherwise.

## Development

1. Create a branch for your change.
2. Use the existing Python virtual environment and dependencies.
3. Keep Python changes compatible with the supported Windows versions.
4. Build the native credential provider with `credential_provider\build.bat` when changing C++ code.
5. Run focused tests or syntax checks for the files you changed.

## Pull requests

Explain the problem, summarize the implementation, and include validation steps. Document user-visible behavior changes in `README.md` and add a `CHANGELOG.md` entry when appropriate.

Never commit generated binaries, local configuration, enrollment data, credentials, or secrets.
