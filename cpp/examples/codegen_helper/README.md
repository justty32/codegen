# `codegen_helper.hpp` — C++ block helper

A single-header, dependency-free (C++17 standard library only) convenience API
for **codegen block scripts written in C++**. It is the C++ counterpart of
`codegen_helper.py`.

Header location: [`cpp/include/codegen_helper.hpp`](../../include/codegen_helper.hpp)

A codegen block can be written in any language its shebang can run. When that
language is C++, this header lets the block read and write the same per-scope
state (global / file / block) that Python blocks and the codegen runner share —
the values round-trip across languages because they live in the same JSON files.

---

## Why a header instead of a module

The Python helper is importable because Python blocks run under an interpreter.
A C++ block must be *compiled* before it runs, so the helper ships as a header
you `#include`. It has **no third-party dependencies** (it does not need the
`nlohmann_json` the codegen binary itself uses) — just drop it on the include
path and `#include "codegen_helper.hpp"`.

---

## API

Everything lives in namespace `codegen` (alias it to `cg` for brevity).

### Scopes

Three scopes, matching the Python helper. Each is a JSON object persisted to a
file that codegen points at via an environment variable:

| Accessor          | Env var          | Lifetime / sharing                          |
| ----------------- | ---------------- | ------------------------------------------- |
| `cg::global()`    | `CODEGEN_GLOBAL` | shared across all files in the run          |
| `cg::file()`      | `CODEGEN_FILE`   | shared across all blocks in the current file |
| `cg::block()`     | `CODEGEN_BLOCK`  | local to the current block                  |

Each scope object exposes:

```cpp
// existence / raw access
bool                        has(key);
std::optional<std::string>  get_json(key);          // raw JSON text, or nullopt
void                        set_json(key, raw_json); // store raw JSON verbatim
void                        del(key);

// typed getters (return `def` when the key is absent or unparseable)
std::string get_str   (key, def = "");
long long   get_int   (key, def = 0);
double      get_double (key, def = 0.0);
bool        get_bool  (key, def = false);

// typed setters (overloaded for std::string/const char*/int/long long/double/bool)
void set(key, value);
```

### Read-only invocation context

Free functions, mirroring the Python helper:

```cpp
std::string               cg::origin_file();   // file content before codegen ran
std::string               cg::origin_block();  // block content (shebang + body)
std::vector<std::string>  cg::targets();       // targets passed on this invocation
std::string               cg::invoke_cwd();    // cwd codegen was invoked from
std::string               cg::file_path();     // abs path of the file being processed
```

---

## Minimal example

```cpp
#include "codegen_helper.hpp"
#include <cstdio>
namespace cg = codegen;

int main() {
    cg::file().set("count", 3);
    long long n = cg::file().get_int("count");      // -> 3
    cg::global().set("name", "widget");

    for (int i = 0; i < n; ++i)
        std::printf("constexpr int ID_%d = %d;\n", i, i);   // becomes generated code
    return 0;
}
```

Whatever the program prints to **stdout** is the code codegen splices back into
the source file in place of the block.

---

## Notes & gotchas

- **`get_json` returns raw text, verbatim.** A value Python wrote as `[10, 20]`
  comes back as the string `"[10, 20]"` (spaces preserved). This is intentional:
  the helper carries values it does not model (arrays, nested objects) as opaque
  raw JSON so it never corrupts them. For scalars, prefer the typed getters.
- **Unknown keys are preserved.** Writing one key re-serializes the object but
  leaves every other key's value byte-for-byte intact.
- **No locking.** Each call does a full read → mutate → write, exactly like the
  Python helper. Concurrent writers to the same scope race.
- Calling a scope method outside a codegen run (env var unset) throws
  `std::runtime_error`.

---

## Runnable demo

This directory contains a working end-to-end demo of a **self-compiling C++
block**:

- [`run_cpp_block`](./run_cpp_block) — a shebang interpreter. codegen runs it as
  `run_cpp_block <tempfile>`; it strips the shebang, compiles the rest with
  `g++ -std=c++17 -I<cpp/include>`, and execs the binary. The block's stdout is
  the generated code.
- [`demo.sh`](./demo.sh) — generates a source file with a C++ block, runs the
  codegen binary on it, and shows the file before/after plus an idempotence check.

### Run it

```sh
# 1. build the codegen binary once
cmake --build cpp/build

# 2. run the demo
cpp/examples/codegen_helper/demo.sh
```

### The block it expands

```cpp
/* CODEGEN_START
#!/abs/path/to/run_cpp_block
#include "codegen_helper.hpp"
#include <cstdio>
namespace cg = codegen;
int main() {
    int count = (int)cg::file().get_int("palette_size", 4);  // default 4
    std::printf("// %d entries, generated by a C++ block\n", count);
    for (int i = 0; i < count; ++i)
        std::printf("constexpr unsigned COLOR_%d = 0x%06Xu;\n", i, (i * 0x282828) & 0xFFFFFF);
    cg::global().set("last_palette_count", count);
    return 0;
}
CODEGEN_END */
```

expands to:

```cpp
// 4 entries, generated by a C++ block
constexpr unsigned COLOR_0 = 0x000000u;
constexpr unsigned COLOR_1 = 0x282828u;
constexpr unsigned COLOR_2 = 0x505050u;
constexpr unsigned COLOR_3 = 0x787878u;
```

### Using the wrapper in your own blocks

The block shebang must name an interpreter codegen can exec. Two options:

1. **Absolute path** (what `demo.sh` does): `#!/abs/path/to/run_cpp_block`
2. **On `PATH`**: put `run_cpp_block` somewhere on `PATH` and use
   `#!/usr/bin/env run_cpp_block`.

`run_cpp_block` resolves the header via its own location (`../../include`), so
keep it inside the repo, or edit the `inc=` line to point at wherever you
install `codegen_helper.hpp`.

> Note: a C++ block pays a compile cost on every codegen run. The default
> per-pass timeout applies to compile **plus** run, so keep blocks lean or raise
> the timeout in your config if a block does heavy work.
