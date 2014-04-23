Granary+
========

Setup
-----

### Step 0: Make sure you have everything that you need.

1. Get LLVM and clang:
  ```basemake
  sudo apt-get install llvm libc++-src clang-3.5 binutils
  ```

  *Note:* If `clang-3.5` is not available on your distribution, then try getting
  `clang-3.4` or `clang-3.3`.

2. Make sure you have Python 2.7 or above, but not Python 3. 

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
make clean all
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
./bin/debug_user/grr --clients=print_bbs --tools=print_bbs -- ls
```

If you want to compile Granary in standalone mode (where it will not take over
some binary's execution via `LD_PRELOAD`, do:

```basemake
make clean ; make all GRANARY_STANDALONE=1
```

Then you can run:

```basemake
./bin/debug_user/granary.out
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

### Step 3: Compiling tools.

First, you need to create `/usr/local/include/granary/granary.h`. This header
file is Granary's client interface, and is derived from Granary's source code.
This header file is created by running:

```basemake
sudo make headers
```

#### User Space

```basemake
make clean_clients clients GRANARY_CLIENTS="follow_jumps print_bbs"
./bin/debug_user/grr --clients=follow_jumps,print_bbs --tools=follow_jumps,print_bbs -- ls
```

#### Kernel Space

If you are compiling Granary clients (and their tools) against a custom kernel, run:

```basemake
make clean_clients clients GRANARY_CLIENTS="follow_jumps print_bbs" GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path-to-kernel-source>
```
