# Repository Guidelines

## Project Structure & Module Organization

TKET combines a C++ circuit compiler with Python bindings. The core library is in `tket/` (`include/`, `src/`, `test/`, and `proptest/`). Python bindings, package code, and tests are in `pytket/` (`binders/`, `pytket/`, and `tests/`); documentation is under `pytket/docs/`. Reusable C++ libraries such as `tklog`, `tkrng`, `tktokenswap`, and `tkwsm` live in `libs/`, each with its own source and test directories. The experimental C API is in `tket-c-api/`. CI workflows are in `.github/workflows/`. Keep generated build and coverage output in ignored directories such as `tket/build/` or `build/`.

## Build, Test, and Development Commands

Use Python 3.10+, CMake 3.26+, and Conan 2; configure a Conan profile and the `tket-libs` remote as described in `README.md`.

```sh
conan build tket --user=tket --channel=stable --build=missing -o "boost/*":header_only=True -o with_all_tests=True
cd pytket && pip install -e . -v
cd tests && pytest
```

The first command builds the C++ core and runs unit/property tests; use `with_test=True` or `with_proptest=True` for a narrower run. For focused C++ runs, execute `tket/build/Release/test/test-tket` and add `"[long]"` for long tests. Library-specific build and test recipes are documented in `libs/README.md`.

## Coding Style & Naming Conventions

Format C++20-compatible code with clang-format v22 using `.clang-format` (`./do-clang-format` formats the repository). Follow Google C++ style and add Doxygen documentation to public headers. Keep standalone `TKET_ASSERT(...);` statements on their own lines. Format Python with Ruff’s default formatter; run Ruff and `pre-commit run --all-files` before submitting. Run mypy from `pytket/` with `mypy --config-file=mypy.ini -p pytket -p tests`.

## Testing and Versioning

Add tests for features and regression tests for bug fixes. Python tests belong in `pytket/tests`; C++ tests follow the relevant `test/` directory. Coverage must not decrease. Changes under `tket/src` require a semantic-version bump in `TKET_VERSION`; changes to a library under `libs/` require that library’s version bump and a focused PR.

## Commit and Pull Request Guidelines

Use short, imperative commit subjects; the history also accepts scoped conventional prefixes such as `fix:`. Keep commits focused. Open PRs against `main` from a fork, explain the behavior and design, list validation commands and coverage impact, and update relevant documentation. Include linked issues when applicable.
