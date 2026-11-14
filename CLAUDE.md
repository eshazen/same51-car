# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A bare-metal LED blink for the **ATSAME51J20A** (Cortex-M4F) on a **SAM E51 Curiosity Nano** board, built with a plain Makefile and `arm-none-eabi-gcc`. It doesn't use Harmony, ASF or CMSIS-Toolbox.

- `src/main.c`: configures DFLL48M (open loop) → GCLK0 → 48 MHz CPU, sets a 1 ms SysTick, and toggles user LED0 on **PA14** (active-low) every 500 ms.
- `vendor/dfp/`: a subset of `packs/Microchip.SAME51_DFP.3.10.289.atpack` (`include/`, `gcc/system_*.c`, `gcc/gcc/startup_*.c`, `gcc/gcc/*_flash.ld`). It is unmodified vendor code.
- `vendor/CMSIS/Core/Include/`: the CMSIS 6.1 Core(M) headers (`core_cm4.h` and dependencies). The DFP doesn't ship these.
- `packs/`: the original packs, kept locally and not tracked by git (ignored). The build doesn't use them; `ARM.CMSIS-Compiler` is not used at all.

## Build

```
make                      # -> build/blink.elf, blink.bin, blink.hex, blink.map
make flash                # build + program over USB via the on-board nEDBG (OpenOCD, board/microchip_same51_curiosity_nano.cfg)
make DEVICE=same51j19a    # any of same51{g18a,g19a,j18a,j19a,j20a,n19a,n20a}; default same51j20a
make clean
```

There are no tests or lint target. The build should stay free of warnings under `-Wall -Wextra`.

## Gotchas

- `make flash` needs OpenOCD ≥ 0.12 and USB access to the nEDBG (03eb:2175). The udev rules in `60-openocd.rules` already grant that here. To use a different probe, override `OPENOCD_CFG=`.
- The build links with `-nostartfiles`. The DFP startup provides `Reset_Handler`, and newlib's `crt0.o` would pull in `exit`/stdio and the `nosys` syscall-stub warnings. Because `crti.o` is also dropped, `main.c` defines an empty `_init()` for `__libc_init_array()`.
- Before reconfiguring the DFLL, `main.c` moves GCLK0 onto OSCULP32K. Stopping the DFLL while the CPU runs from it would hang.
- The flash wait states are fixed at `RWS=2` with `AUTOWS` cleared. That is conservative for 48 MHz.
- `.atpack` files are zip archives. Inspect them with `unzip -l` and pull single files with `unzip -p`. Don't extract them into the repo root.
