Granary
=======

Setup
-----

### Step 0: Make sure you have everything that you need.

1. Get LLVM and clang:
  ```
  sudo apt-get install clang-3.5 llvm libc++-dev libc++1 binutils
  ```

  **Note:** If `clang-3.5` is not available on your distribution, then try getting
  `clang-3.4` or `clang-3.3`.

  **Note:** Granary uses `llvm-link-3.5` by default. If you don't have it, then
  you can manually specify a different version of `llvm-link`. For example,
  `make all GRANARY_LLVM_LINK=llvm-link` (this will use the system's default
  version of `llvm-link`).

2. Make sure you have Python 2.7 or above, but not Python 3.
  
  **Note:** If you have multiple Python 2.7 is not your system default, then
  you can still specify the path to the Python 2.7. For example: 
  `make all GRANARY_PYTHON=/usr/bin/python-2.7`.

### Step 1: Initial setup.

This does things like fetch dependencies.

```
make setup
```

### Step 2: Compiling Granary.
#### Test Cases
Be sure to run Granary through its paces first. These tests are definitely not
exhaustive, but can help to determine if things are generally in working order:

```
make clean test
```

Or

```
make clean all
```

For release builds, run:

```
make clean all GRANARY_TARGET=release
```

### Step 3: Run Granary.

You can use Granary's "injector" (called `grr`) to inject Granary into a
process. Below is an example where Granary is injected into `ls` using `grr`.

```
./bin/debug_linux_user/grr -- ls
```

Command-line arguments can be passed to `grr`, and they will be forwarded to
Granary itself. For example, if you've already compiled Granary's clients (see
below) then you could try the following:

```
./bin/debug_linux_user/grr --tools=watchpoints -- ls
```

The list of available command-line arguments can be seen by invoking:

```
./bin/debug_linux_user/grr --help -- ls
```

Notice that some options have a tool name, shown in green, listed beside them.
That means that those options are tool-specific and are ignored when that tool
is not used.

Okay, time to actually do something. Lets see what system calls are executed by
a program. To do this, tell Granary to use the system call tracing tool:

```
./bin/debug_linux_user/grr --tools=strace -- ls
```

You'll need to press enter to get past the "debug GDB prompt". This is a
convenience feature for Granary developers. It enables GDB-based debugging of
Granary by pausing the program and waiting for the user to press Enter, thus
giving the developer plenty of time to attach GDB to the running process.
Granary comes with its own `.gdbinit` file to improve the GDB-based debugging
experience.

Here's an example where we disable the prompt:

```
./bin/debug_linux_user/grr --tools=strace --no_debug_gdb_prompt -- ls
```

Many Granary tools, e.g. `memop`, don't provide any kind of user interface.
Instead, they implement common functionality (e.g. generic memory operand
interposition) and hooks for other tools to use to achieve common tasks. For
example, the `poly_code` tool uses the `watchpoints` tool, which then uses
the `memop` tool.

