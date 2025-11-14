# wcircle
Wayland Circular Scroll Daemon

# Build

```bash
gcc wcircle.c -o wcircle.bin $(pkg-config --cflags --libs libevdev) -lm
```

# Before Running

Use the `evtest` command to identify your touchpad device.

```bash
evtest
```

From the output, locate your touchpad and note the corresponding device file
(for example, `/dev/input/eventX`).

# Run

Root privileges are required:

```bash
sudo ./wcircle.bin /dev/input/eventX
```

# Troubleshooting

If you encounter libevdev-related errors during compilation, check the location of `libevdev.h`:

```bash
find / -name "libevdev.h" 2>/dev/null
```

On my system, it was located at:

```
/usr/include/libevdev-1.0/libevdev/libevdev.h
```

However, this may differ depending on your environment.
If the location is different, modify the include path at the beginning of `wcircle.c`:

```c
#include <libevdev-1.0/libevdev/libevdev.h>   // <-- Update this path
```

---
