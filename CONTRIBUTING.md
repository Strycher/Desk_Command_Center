# Contributing to Desk Command Center

This is a personal project — a touchscreen dashboard running on the Elecrow
CrowPanel Advance 5.0" (ESP32-S3). Contributions are welcome but not expected,
and response times may vary.

## Before You Start

- **Issues may go unanswered.** This is a hobby project maintained on personal time.
- **PRs are more useful than issues.** If you find a bug, a fix is more helpful than a report.
- **No feature requests please.** This device is tailored to a specific workflow.
  Fork the project and customize it for your own setup.

## Development Setup

### Firmware (ESP32-S3)
- **IDE:** PlatformIO (VSCode recommended)
- **Board:** CrowPanel Advance 5.0" — `esp32-s3-devkitc-1` with custom config
- **Build:** `cd firmware && pio run`

### Bridge (Python)
- **Runtime:** Python 3.11+
- **Install:** `pip install -r bridge/requirements.txt`
- **Test:** `PYTHONPATH=bridge pytest bridge/tests/ -v`
- **Lint:** `ruff check bridge/`

### Secrets Guard

After cloning, enable the pre-commit hook to prevent accidental secret commits:

```bash
git config core.hooksPath .githooks
```

## Pull Request Guidelines

1. **One concern per PR.** Don't mix bug fixes with refactors.
2. **Test your changes.** Firmware must compile. Bridge must pass `pytest`.
3. **Follow existing style.** See `CLAUDE.md` for naming conventions.
4. **Keep commits atomic.** Use conventional commit format: `type(scope): description`

## Code Style

| Element       | Convention         | Example                |
|---------------|--------------------|------------------------|
| Source files  | `snake_case`       | `display_manager.cpp`  |
| Classes       | `PascalCase`       | `DisplayManager`       |
| Functions     | `camelCase`        | `updateStatusBar()`    |
| Constants     | `UPPER_SNAKE`      | `SCREEN_WIDTH`         |
| Private members | `_prefix`        | `_initialized`         |

## License

By contributing, you agree that your contributions will be licensed under the
MIT License that covers this project.
