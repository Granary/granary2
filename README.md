Granary
=======

Check out the [Wiki](https://github.com/Granary/granary2/wiki)! It describes
in more detail how to build the Linux kernel, set up a VM, and more.

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

This step creates some convenient symbolic links to your current Linux kernel
build and virtual machine image. This initial setup is only needed if Granary
will be used for kernel space instrumentation.

```
./scripts/make_linux_build_link.sh /path/to/kernel
./scripts/make_vmlinux_link.sh /path/to/kernel/vmlinux
./scripts/make_qemu_img_link.sh /path/to/qmeu/vm.img
```

Finally, make sure everything is set up for Granary. This does things like fetch dependencies.

```
make setup
```

### Step 2: Compiling Granary.
#### Test Cases
Be sure to run Granary through its paces first. These tests are definitely not
exhaustive, but can help to determine if things are generally in working order:

```
make clean test GRANARY_TARGET=test
```

#### User Space
If you are compiling Granary for user space, run:

```
make clean all
```

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

Granary's injector doesn't actually understand Granary arguments, hence the
requirement of specificying *some* executable to instrument.

#### Kernel Space

If you are compiling Granary against your running kernel, run:

```
make clean all GRANARY_WHERE=kernel
```

If you are compiling Granary against a custom kernel, run:

```
make clean all GRANARY_WHERE=kernel
```
