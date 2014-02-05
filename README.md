Granary+
========

Setup
-----

### Step 1: Initial setup.

```basemake
./scripts/make_vmlinux_link.sh <path to your kernel's vmlinux file>
./scripts/make_qemu_img_link.sh <path to your QEMU VM image>
```

### Step 2: Compiling Granary.

If you are compiling Granary for user space, run:

```basemake
make clean ; make all
```

If you are compiling Granary against your running kernel, run:

```basemake
make clean ; make all GRANARY_WHERE=kernel
```

If you are compiling Granary against a custom kernel, run:

```basemake
make clean ; make all GRANARY_WHERE=kernel GRANARY_KERNEL_DIR=<path to your kernel source code>
```

### Step 3: Compiling tools.
