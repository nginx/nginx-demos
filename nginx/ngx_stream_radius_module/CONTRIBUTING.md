# Contributing to ngx_stream_radius_module

Thank you for your interest in contributing! This document explains the development workflow.

## Getting Started

```bash
git clone https://github.com/nginx/nginx-demos.git
cd nginx/ngx_stream_radius_module

# Run tests immediately to verify your environment
cd tests && make && cd ..
```

## Development Workflow

1. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feature/my-new-feature
   ```

2. **Make your changes** in `src/`.

3. **Add or update tests** in `tests/test_radius_parser.c`.
   Every bug fix must include a test that reproduces the bug.

4. **Run the full test suite**:
   ```bash
   cd tests && make
   ```
   All tests must pass with zero failures.

5. **Verify against real NGINX** if you changed directive handling or variable registration:
   ```bash
   make NGINX_SRC=/path/to/nginx dynamic
   nginx -t
   ```

6. **Open a Pull Request** with:
   - A description of what changed and why
   - Mention of any RFC sections relevant to the change
   - Test output showing all tests pass

## Code Style

This project follows the NGINX C coding conventions:

- **Indentation**: 4 spaces (no tabs)
- **Braces**: K&R style (opening brace on same line for functions: no; for control flow: yes)
- **Naming**: `ngx_stream_radius_*` for public API, `ngx_radius_*` for internal helpers
- **Memory**: Use `ngx_pool_t` exclusively. No `malloc`/`free`/`strdup`.
- **Strings**: Use `ngx_str_t`. Do not use `strlen` on NGINX strings.
- **Errors**: Always log with `ngx_log_error`. Return `NGX_ERROR` on failure.
- **Line length**: 80 characters preferred, 100 maximum.

## Adding a New Dictionary Keyword

If you add support for a new FreeRADIUS dictionary keyword (e.g., `$INCLUDE`):

1. Add parsing logic in `ngx_radius_dict_parse_line()` in `src/ngx_stream_radius_dict.c`
2. Add a test dictionary fragment in `tests/`
3. Add a test case in `tests/test_radius_parser.c`
4. Document the keyword in `README.md` under Dictionary File Format

## Adding Support for a New Attribute Data Type

1. Add the `NGX_RADIUS_TYPE_*` constant in `src/ngx_stream_radius_module.h`
2. Add the decode case in `ngx_radius_decode_value()` in `src/ngx_stream_radius_parser.c`
3. Add the keyword mapping in `ngx_radius_parse_type_keyword()` in `src/ngx_stream_radius_dict.c`
4. Add the type to the test shim in `tests/test_shim.h`
5. Add a test case with a packet containing that type

## Reporting Bugs

Please open a GitHub Issue with:
- NGINX version (`nginx -v`)
- NGINX Plus or Open Source
- OS and version
- Module version or commit hash
- A minimal `nginx.conf` reproducing the issue
- A hex dump of the RADIUS packet (e.g., from Wireshark/tshark) if the issue is parsing-related
- The observed vs. expected behavior

## Security Issues

**Do not open a public GitHub Issue for security vulnerabilities.**

Please email the maintainers directly. We aim to respond within 72 hours and will coordinate a responsible disclosure timeline.
