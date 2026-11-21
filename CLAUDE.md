# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal test projects for the **ATSAME51J20A** (Cortex-M4F) on a **SAM E51 Curiosity Nano** board. Each one is built with its own plain Makefile and `arm-none-eabi-gcc`. They don't use Harmony, ASF or CMSIS-Toolbox. All projects share `vendor/` at the repo root (`DFP := ../vendor/dfp`).

- `blink/`: `src/main.c` configures DFLL48M (open loop) → GCLK0 → 48 MHz CPU, sets a 1 ms SysTick, and toggles user LED0 on **PA14** (active-low).
- `sdcard/`: a microSD interface test in SPI mode on **SERCOM1** (function C). The pins are MOSI=PA16 (PAD0), SCK=PA17 (PAD1), MISO=PA19 (PAD3, internal pull-up) and CS=PA08 (GPIO). There is no card-detect line. `src/sd_spi.c` handles init (CMD0/8/ACMD41/58), enables CRC checking with CMD59, reads the CSD/CID, and does single-block reads with CRC16 checks. The SPI clock is 400 kHz during init and 8 MHz afterwards. `src/main.c` reads blocks 0..63 and reports on LED0: a 1 Hz blink means pass, and N flashes then a pause means failure, where N is the `sd_status_t` value. Results are kept in the global `g_test` for gdb. The clock setup is copied from `blink/`.
- `vendor/dfp/`: a subset of `packs/Microchip.SAME51_DFP.3.10.289.atpack` (`include/`, `gcc/system_*.c`, `gcc/gcc/startup_*.c`, `gcc/gcc/*_flash.ld`). It is unmodified vendor code.
- `vendor/CMSIS/Core/Include/`: the CMSIS 6.1 Core(M) headers (`core_cm4.h` and dependencies). The DFP doesn't ship these.
- `packs/`: the original packs, kept locally and not tracked by git (ignored). The build doesn't use them; `ARM.CMSIS-Compiler` is not used at all.

## Build

Run these from inside a project directory (`blink/`, `sdcard/`), or use `make -C <project>`:

```
make                      # -> build/<project>.elf, <project>.bin, .hex, .map
make flash                # build + program over USB via the on-board nEDBG (OpenOCD, board/microchip_same51_curiosity_nano.cfg)
make DEVICE=same51j19a    # any of same51{g18a,g19a,j18a,j19a,j20a,n19a,n20a}; default same51j20a
make clean
```

There are no tests or lint target. The build should stay free of warnings under `-Wall -Wextra`.

## Gotchas

- `make flash` needs OpenOCD ≥ 0.12 and USB access to the nEDBG (03eb:2175). The udev rules in `60-openocd.rules` already grant that here. To use a different probe, override `OPENOCD_CFG=`.
- The build links with `-nostartfiles`. The DFP startup provides `Reset_Handler`, and newlib's `crt0.o` would pull in `exit`/stdio and the `nosys` syscall-stub warnings. Because `crti.o` is also dropped, each `main.c` defines an empty `_init()` for `__libc_init_array()`.
- Before reconfiguring the DFLL, `main.c` moves GCLK0 onto OSCULP32K. Stopping the DFLL while the CPU runs from it would hang.
- The flash wait states are fixed at `RWS=2` with `AUTOWS` cleared. That is conservative for 48 MHz.
- PA16 as MOSI only works with SERCOM1. On SERCOM3 it would be PAD1, and DOPO can't route the SPI output to PAD1. That forces SCK onto PA17.
- `.atpack` files are zip archives. Inspect them with `unzip -l` and pull single files with `unzip -p`. Don't extract them into the repo root.
