Granary+
========

Check out the Wiki!

Setup
-----

### Step 0: Make sure you have everything that you need.

1. Get LLVM and clang:
  ```basemake
  sudo apt-get install clang-3.5 llvm libc++-dev libc++1 binutils
  ```

  *Note:* If `clang-3.5` is not available on your distribution, then try getting
  `clang-3.4` or `clang-3.3`.

  *Note:* Verify that `llvm-link-3.5` is installed on your system. If you don't
  have it, then you can manually specify a different version of `llvm-link`. For
  example, `make all GRANARY_LLVM_LINK=llvm-link`  (this will use the system 
  default `llvm-link`).

2. Make sure you have Python 2.7 or above, but not Python 3. If you have
  multiple Python versions on your machine, but 2.7 is not the default, then
  you can still specify the path to the Python 2.7. For example: 
  `make all GRANARY_PYTHON=/path/to/python`.

### Step 1: Initial setup.

This step creates some convenient symbolic links to your current Linux kernel
build and virtual machine image. This initial setup is only needed if Granary
will be used for kernel space instrumentation.

```basemake
./scripts/make_vmlinux_link.sh <path-to-kernel-vmlinux>
./scripts/make_qemu_img_link.sh <path-to-QEMU-VM-image>
```

### Step 2: Compiling Granary.
#### User Space
If you are compiling Granary for user space, run:

```basemake
make clean
make all -j
```

You can use Granary's "injector" (called `grr`) to inject Granary into a
process. Below is an example where Granary is injected into `ls` using `grr`.

```basemake
./bin/debug_user/grr -- ls
```

Command-line arguments can be passed to `grr`, and they will be forwarded to
Granary itself. For example, if you've already compiled Granary's clients (see
below) then you could try the following:

```basemake
./bin/debug_user/grr --tools=watchpoints -- ls
```

#### Kernel Space

If you are compiling Granary against your running kernel, run:

```basemake
make clean all GRANARY_WHERE=kernel
```

If you are compiling Granary against a custom kernel, run:

```basemake
make clean all GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```
