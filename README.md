# wcircle
Wayland Circular Scroll Daemon

# Build

```bash
gcc wcircle.c -o wcircle.bin $(pkg-config --cflags --libs libevdev) -lm
```

# Run

Root privileges are required:

```bash
sudo ./wcircle.bin
```

## Configuration

wcircle supports a minimal INI configuration file that can override a few runtime parameters. The parser is provided by the lightweight "inih" library; the program will look for the config file in these locations (in order):

- `/etc/wcircle/config.ini`
- `./config.ini` (working directory)

If no config file is found, the default settings will be used.

Sample `config.ini`:

```ini
[wcircle]
outer_ratio_min=0.7   ; inner boundary of touch ring (relative to center)
outer_ratio_max=1.4   ; outer boundary of touch ring
start_arc_deg=5       ; angle to begin scrolling (degrees)
step_deg=18           ; degrees per wheel step
wheel_step=1          ; integer wheel step value sent with REL_WHEEL
wheel_hi_res=0        ; use high-resolution wheel if available (1=yes, 0=no)
invert_scroll=0       ; invert scroll direction (1=yes, 0=no)
pad_device_path=/dev/input/event0 ; if you want to explicitly specify touchpad device
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
