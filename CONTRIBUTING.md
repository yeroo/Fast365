# Contributing to Fast365

Thanks for your interest in improving Fast365!

## Building (Windows)

```
cmake -S . -B build -A x64
cmake --build build --config Release
```

or simply run `build.bat`. The result is `build\Release\fast365.exe`.

## Testing

Run the binary against a `.docx` and inspect the generated HTML:

```
build\Release\fast365.exe sample.docx -o sample.html
```

## Guidelines

- C++17, MSVC. **No third-party dependencies** — standard library only.
- Keep the build `/W4` clean.
- Keep changes focused; one logical change per pull request.
- If you change behavior, describe it in the PR (and update the README if it is
  user-facing).

## Reporting issues

Open an issue with the exact command you ran and, if possible, a minimal `.docx`
that reproduces the problem.
