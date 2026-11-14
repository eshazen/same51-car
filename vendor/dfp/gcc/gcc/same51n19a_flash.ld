/*
 * GCC linker script (flash) for ATSAME51N19A
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*------------------------------------------------------------------------------
 *      Linker script for running in internal FLASH on the ATSAME51N19A
 *----------------------------------------------------------------------------*/

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)
SEARCH_DIR(.)

/*************************************************************************
 * Memory-Region Definitions
 * You may define these as options to the linker when calling
 * the linker via the arm-gcc shell.
 * ie. '-Wl,--defsym=<memory_type>_ORIGIN=<address>'
 * and/or '-Wl,--defsym=<memory_type>_LENGTH=<size>'
 *************************************************************************/

/*---------------------- ROM Configuration ----------------------------------
  <h> ROM Configuration
    <o0> ROM Base Address <0x0-0xFFFFFFFF:8>
    <o1> ROM Size (in Bytes) <0x0-0xFFFFFFFF:8>
  </h>
  -----------------------------------------------------------------------------*/
ROM_ORIGIN = DEFINED(ROM_ORIGIN) ? ROM_ORIGIN : 0x00000000;
ROM_LENGTH = DEFINED(ROM_LENGTH) ? ROM_LENGTH : 0x80000; /* rom, 512 KB */
/*---------------------- RAM Configuration ----------------------------------
  <h> RAM Configuration
    <o0> RAM Base Address <0x0-0xFFFFFFFF:8>
    <o1> RAM Size (in Bytes) <0x0-0xFFFFFFFF:8>
  </h>
  -----------------------------------------------------------------------------*/
RAM_ORIGIN = DEFINED(RAM_ORIGIN) ? RAM_ORIGIN : 0x20000000;
RAM_LENGTH = DEFINED(RAM_LENGTH) ? RAM_LENGTH : 0x30000; /* ram, 192 KB */
/*---------------------- TCM Configuration ----------------------------------
  <h> TCM Configuration
    <o0> TCM Base Address <0x0-0xFFFFFFFF:8>
    <o1> TCM Size (in Bytes) <0x0-0xFFFFFFFF:8>
  </h>
  Override via -Wl,--defsym=__STARTUP_TCM_LENGTH=<size> on the linker command line.
  Set __STARTUP_TCM_LENGTH=0 to disable this TCM region entirely.
  NOTE: TCM_Enable() is called in Reset_Handler before .data copy.
  -----------------------------------------------------------------------------*/
__STARTUP_TCM_ORIGIN = DEFINED(__STARTUP_TCM_ORIGIN) ? __STARTUP_TCM_ORIGIN : 0x03000000;
__STARTUP_TCM_LENGTH = DEFINED(__STARTUP_TCM_LENGTH) ? __STARTUP_TCM_LENGTH : 0x1000;

/* The stack and heap size used by the application. NOTE: you need to adjust according to your application. */
/*--------------------- Stack / Heap Configuration ----------------------------
  <h> Stack / Heap Configuration
    <o0> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
    <o1> Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
  </h>
  -----------------------------------------------------------------------------*/
STACK_SIZE = DEFINED(STACK_SIZE) ? STACK_SIZE : DEFINED(__stack_size__) ? __stack_size__ : 0x0400;
HEAP_SIZE = DEFINED(HEAP_SIZE) ? HEAP_SIZE : DEFINED(__heap_size__) ? __heap_size__ : 0x0200;

/* ARMv8-M stack sealing:
   to use ARMv8-M stack sealing set __STACKSEAL_SIZE to 8 otherwise keep 0
 */
__STACKSEAL_SIZE = DEFINED(__STACKSEAL_SIZE) ? __STACKSEAL_SIZE : 0;

/*************************************************************************
 * Memory-Region Definitions
 * The MEMORY command describes the location and size of blocks of memory
 * on the target device. The command below uses the macros defined above.
 *************************************************************************/
MEMORY
{
    rom (rx)    : ORIGIN = ROM_ORIGIN, LENGTH = ROM_LENGTH
    ram (rwx)   : ORIGIN = RAM_ORIGIN, LENGTH = RAM_LENGTH
    /* TCM regions — enabled at startup by TCM_Enable() in Reset_Handler */
    tcm (rwx)   : ORIGIN = __STARTUP_TCM_ORIGIN, LENGTH = __STARTUP_TCM_LENGTH
    /* Device configuration memory regions */
    /* Use __attribute__((section(".<section_name>"))) to place data in these regions */
    sw0                  (rw!x)  : ORIGIN = 0x00800080, LENGTH = 0x10
    temp_log             (rw!x)  : ORIGIN = 0x00800100, LENGTH = 0x200
    user_page            (rw!x)  : ORIGIN = 0x00804000, LENGTH = 0x200
}

ENTRY(Reset_Handler)

/*************************************************************************
 * ELF Program Header Declarations (PHDRS)
 *
 * Without an explicit PHDRS block the linker auto-generates PT_LOAD
 * segments.  Because .relocate has its LMA in flash (AT _etext) but its
 * VMA in RAM, the auto-generated segment would span both the executable
 * code and the writable data initialiser image, resulting in a single
 * PT_LOAD with R+W+X permissions.  GNU ld 2.39 / GCC 12 and later emit:
 *
 *   warning: <elf> has a LOAD segment with RWX permissions
 *
 * The fix is to declare two explicit PT_LOAD segments with precise flags:
 *
 *   flash_rx  — read + execute (FLAGS = R|X = 5)
 *               Covers all code and read-only data that lives in flash.
 *               Sections: .text, .ARM.exidx, .eh_frame_hdr, .eh_frame, .gcc_except_table,
 *                         .copy.table, .zero.table
 *
 *   flash_rw  — read + write  (FLAGS = R|W = 6)
 *               Covers the initialised-data image that is stored in flash
 *               (LMA) but copied to RAM at startup (VMA).
 *               Section: .relocate
 *
 * NOLOAD sections (.bss, .heap, .stack) are not part of any LOAD segment
 * and therefore carry no phdr assignment — the linker omits them from the
 * ELF program-header table automatically.
 *
 * Additional PT_LOAD segments are emitted for each writable device
 * configuration region (user_page, fuses, user_signatures, …). These regions
 * sit at non-contiguous flash addresses outside the main code segment, so
 * the linker cannot automatically place them inside flash_rx or flash_rw.
 * Without a dedicated LOAD segment, the linker either rejects the section
 * ("can't be allocated in segment …") or the content is silently dropped
 * from the hex/binary output — preventing users from programming fuses or
 * user-row data via GCC.
 *
 * Note: lld (LLVM linker) also supports the PHDRS syntax, so this fix is
 * compatible with both GNU ld and Clang/lld toolchains.
 *************************************************************************/
PHDRS
{
    flash_rx  PT_LOAD FLAGS(5);  /* R(4) + X(1) — executable code in flash    */
    flash_rw  PT_LOAD FLAGS(6);  /* R(4) + W(2) — initialised data init image */
    tls       PT_TLS  FLAGS(4);  /* R(4) — initial TLS template block (.tdata + .tbss),
                                     copied into each thread's TLS block by picolibc's
                                     _init_tls(); flags are informational only on a
                                     bare-metal static image (no dynamic loader) */
    sw0       PT_LOAD FLAGS(6);  /* R(4) + W(2) — device configuration region */
    temp_log  PT_LOAD FLAGS(6);  /* R(4) + W(2) — device configuration region */
    user_page PT_LOAD FLAGS(6);  /* R(4) + W(2) — device configuration region */
}

/* Section Definitions */
SECTIONS
{
    .text :
    {
        . = ALIGN(4);
        _sfixed = .;
        KEEP(*(.vectors .vectors.*))
        *(.text .text.* .gnu.linkonce.t.*)
        *(.glue_7t) *(.glue_7)
        *(.rodata .rodata* .gnu.linkonce.r.*)
        *(.got .got.*)
        *(.ARM.extab* .gnu.linkonce.armextab.*)

        /* Support C constructors, and C destructors in both user code
           and the C library. This also provides support for C++ code. */
        . = ALIGN(4);
        KEEP(*(.init))
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP (*(.preinit_array))
        __preinit_array_end = .;

        . = ALIGN(4);
        __init_array_start = .;
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array))
        __init_array_end = .;

        . = ALIGN(4);
        KEEP (*crtbegin.o(.ctors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .ctors))
        KEEP (*(SORT(.ctors.*)))
        KEEP (*crtend.o(.ctors))

        . = ALIGN(4);
        KEEP(*(.fini))

        . = ALIGN(4);
        __fini_array_start = .;
        KEEP (*(.fini_array))
        KEEP (*(SORT(.fini_array.*)))
        __fini_array_end = .;

        KEEP (*crtbegin.o(.dtors))
        KEEP (*(EXCLUDE_FILE (*crtend.o) .dtors))
        KEEP (*(SORT(.dtors.*)))
        KEEP (*crtend.o(.dtors))

        . = ALIGN(4);
        _efixed = .;            /* End of text section */
    } > rom  :flash_rx

    /* .ARM.exidx is sorted, so has to go in its own output section.  */
    PROVIDE_HIDDEN (__exidx_start = .);
    .ARM.exidx :
    {
      *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > rom  :flash_rx
    PROVIDE_HIDDEN (__exidx_end = .);

    /* Unwind metadata is read-only and belongs with code, not a later config region. */
    .eh_frame_hdr :
    {
        *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*)
    } > rom  :flash_rx

    .eh_frame :
    {
        *(.eh_frame .eh_frame.*)
    } > rom  :flash_rx

    .gcc_except_table :
    {
        *(.gcc_except_table .gcc_except_table.*)
    } > rom  :flash_rx

    .copy.table :
    {
        . = ALIGN(4);
        __copy_table_start__ = .;

        LONG (LOADADDR(.relocate))
        LONG (ADDR(.relocate))
        LONG (SIZEOF(.relocate) / 4)

        /* Add each additional data section here */
        /*
            LONG (LOADADDR(.data2))
            LONG (ADDR(.data2))
            LONG (SIZEOF(.data2) / 4)
        */
        __copy_table_end__ = .;
    } > rom  :flash_rx

    .zero.table :
    {
        . = ALIGN(4);
        __zero_table_start__ = .;

        /*  .bss initialization to zero is already done during C Run-Time Startup.
        LONG (ADDR(.bss))
        LONG (SIZEOF(.bss) / 4)
        */

        /* Add each additional bss section here */
        /*
            LONG (ADDR(.bss2))
            LONG (SIZEOF(.bss2) / 4)
        */
        __zero_table_end__ = .;
    } > rom  :flash_rx

    . = ALIGN(4);
    /* _etext  : newlib crt0/crt1 uses this as the LMA source address from
     *           which the .data section is copied into RAM at startup.
     * __etext : alias used by some CMSIS-style startup files. */
    _etext = .;
    __etext = .;

    .relocate : AT (_etext)
    {
        . = ALIGN(4);
        /* _srelocate / _erelocate : legacy Microchip/ASF startup files
         *   (startup_*.c shipped in older Atmel Software Framework packs)
         *   iterate between these two symbols to copy .data from flash. */
        _srelocate = .;
        /* __data_start__ / __data_end__ : ARM CMSIS startup and
         *   newlib-nano (nano.specs) startup copy .data between these. */
        __data_start__ = .;
        *(vtable);
        *(.ramfunc .ramfunc.*);
        *(.data .data.*);
        . = ALIGN(4);
        _erelocate = .;
        __data_end__ = .;
    } > ram  :flash_rw

    /* Picolibc initial TLS data block (errno and other __thread libc state).
     * Contiguous with .relocate in both flash (LMA) and RAM (VMA) so a single
     * copy loop (driven by __data_start/__data_end/__data_source) initializes
     * both regular .data and the TLS template together, per picolibc's own
     * reference linker script:
     * https://github.com/picolibc/picolibc/blob/036919b76d7124f25f1d82554a06c921708cd4b1/cmake/TC-arm-none-eabi.ld */
    .tdata : AT (LOADADDR(.relocate) + SIZEOF(.relocate))
    {
        . = ALIGN(4);
        *(.tdata .tdata.* .gnu.linkonce.td.*)
    } > ram  :flash_rw :tls

    /* Picolibc / Clang runtime symbol aliases
     *
     * The DFP maps both GCC and CLANG to these linker scripts via the shared
     * GCC+CLANG condition.  The LLVM embedded toolchain ships picolibc as its
     * C runtime instead of newlib.  Picolibc's startup file (crt0.c) and its
     * TLS helper (set_tls.c) expect symbol names that differ from the GNU ld
     * double-underscore convention defined above.  PROVIDE is used so each
     * alias is only emitted when the name is not already defined elsewhere.
     *
     * .data aliases — referenced by picolibc crt0.c:_start():
     *   __data_start  : VMA (run address) of the first byte of .data.
     *                   GNU ld equivalent: __data_start__
     *   __data_source : LMA (load address, i.e. flash address) from which
     *                   .data is copied at startup.
     *                   GNU ld equivalent: _etext / LOADADDR(.relocate)
     *   __data_size   : Byte length of .data to copy.
     *                   GNU ld equivalent: __data_end__ - __data_start__ */
    PROVIDE(__data_start  = __data_start__);
    PROVIDE(__data_source = LOADADDR(.relocate));

    PROVIDE(__tls_base     = ADDR(.tdata));
    PROVIDE(__tdata_start  = ADDR(.tdata));
    PROVIDE(__tdata_source = LOADADDR(.tdata));
    PROVIDE(__tdata_size   = SIZEOF(.tdata));

    /* Combined .relocate + .tdata copy range, consumed by picolibc crt0's
     * single data-copy loop (distinct from the GNU-style __data_start__/
     * __data_end__ symbols above, which describe .relocate alone and must
     * stay unchanged for non-picolibc/newlib-nano startup compatibility). */
    PROVIDE(__data_end        = __tdata_start + __tdata_size);
    PROVIDE(__data_source_end = __tdata_source + __tdata_size);
    PROVIDE(__data_source_size = __data_source_end - __data_source);
    PROVIDE(__data_size = __data_end - __data_start);
    PROVIDE(__tls_inittls = 0);

    .tbss (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE(__tbss_start = .);
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
        PROVIDE(__tbss_end = .);
    } > ram  :tls

    /* Capture .tbss bounds while processing the output section: linkers may
     * elide an empty NOLOAD section, but picolibc TLS aliases still need the
     * start/end values. */
    PROVIDE(__tbss_offset = __tbss_start - __tdata_start);
    PROVIDE(__tbss_size   = __tbss_end - __tbss_start);
    PROVIDE(__tls_end     = __tbss_end);
    PROVIDE(__tls_size    = __tls_end - __tls_base);
    /* .tdata always survives elision (it is backed by the flash_rw PT_LOAD
     * program header even when empty), so ALIGNOF(.tdata) alone is used as
     * the alignment floor here; ARM32's ABI-mandated minimum TCB offset of
     * 8 bytes (below) covers the common case regardless. */
    PROVIDE(__tls_align   = ALIGNOF(.tdata));

    /* GNU ld does not advance the location counter for a NOLOAD TLS .tbss.
     * This Picolibc-compatible section reserves its extent so .bss cannot
     * overlap it; lld already advances the counter. */
    .tbss_space (NOLOAD) :
    {
        . = ADDR(.tbss) + SIZEOF(.tbss);
    } > ram

    /* __arm32_tls_tcb_offset : Byte offset from the TLS pointer to the
     *   Thread Control Block on ARM32. Must be at least 8 (per the ARM ELF
     *   ABI) but grows to match the actual alignment requirement of the
     *   .tdata/.tbss block so picolibc's _set_tls() places the TCB correctly
     *   for any TLS variable alignment. */
    PROVIDE(__arm32_tls_tcb_offset = MAX(8, __tls_align));

    /* .bss section which is used for uninitialized data */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        /* _sbss / _ebss and _szero / _ezero : legacy Microchip/ASF startup
         *   files zero-initialise .bss between these symbols.  Both pairs
         *   refer to the same addresses; _szero/_ezero is an older naming
         *   convention found in pre-2015 Atmel START / ASF packs. */
        _sbss = . ;
        _szero = .;
        /* __bss_start__ / __bss_end__ : ARM CMSIS startup and newlib-nano
         *   startup zero-initialise .bss between these symbols. */
        __bss_start__ = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = . ;
        _ezero = .;
        __bss_end__ = .;
    } > ram

    /* .bss aliases — referenced by picolibc crt0.c:_start():
     *   __bss_start : VMA of the first byte of .bss.
     *                 GNU ld equivalent: __bss_start__
     *   __bss_size  : Byte length of .bss to zero-initialise.
     *                 GNU ld equivalent: __bss_end__ - __bss_start__ */
    PROVIDE(__bss_start = __bss_start__);
    PROVIDE(__bss_size  = __bss_end__ - __bss_start__);

    /* .noinit section: allocated in RAM but not part of the loadable image
     * and not zero-initialised at startup. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit .noinit.*)
    } > ram

    /* heap section */
    .heap (NOLOAD):
    {
        . = ALIGN(8);
        /* _sheap / _eheap : legacy Microchip/ASF startup files use these to
         *   track the heap region boundaries. */
        _sheap = .;
        /* __end__ : newlib's sbrk() implementation uses this as the initial
         *   heap pointer when growing the heap upward.
         * end      : the standard C symbol for the start of the heap; also
         *   consumed by newlib's sbrk() and by some C++ operator new
         *   implementations. */
        __end__ = .;
        PROVIDE(end = .);
        . = . + HEAP_SIZE;
        . = ALIGN(8);
        _eheap = .;
        /* __HeapLimit : ARM CMSIS startup checks that the heap does not
         *   collide with the stack using this symbol. */
        __HeapLimit = .;
    } > ram

    /* Picolibc heap aliases — referenced by picolibc's sbrk()-based heap
     * allocator, used when the application links malloc/dynamic memory
     * allocation (e.g. CMSIS Driver Validation projects):
     *   __heap_start : VMA of the first byte of the heap (growth base).
     *                  GNU ld equivalent: __end__ / end
     *   __heap_end   : VMA one past the last usable heap byte (growth
     *                  limit / top of the heap region).
     *                  GNU ld equivalent: __HeapLimit */
    PROVIDE(__heap_start = __end__);
    PROVIDE(__heap_end   = __HeapLimit);

    /* Anchor the stack to the physical top of RAM (growing down) so that
     * a stack underflow (excessive pop) faults against the real end of
     * RAM rather than leaving unused RAM above the stack. This matches
     * the placement used by the IAR and XC32 linker scripts and is
     * required to get the full benefit of ARMv8-M stack-limit-register
     * (MSPLIM) based stack protection. __STACKSEAL_SIZE reserves space
     * for the ARMv8-M stack sealing value immediately above the stack
     * when stack sealing is enabled (0 otherwise). */
    . = ORIGIN(ram) + LENGTH(ram) - STACK_SIZE - __STACKSEAL_SIZE;

    /* stack section */
    .stack (NOLOAD):
    {
        . = ALIGN(8);
        /* _sstack / _estack : legacy Microchip/ASF startup files reference
         *   these to locate the stack region. */
        _sstack = .;
        /* __StackLimit / __StackTop : ARM CMSIS startup initialises the
         *   Main Stack Pointer to __StackTop and uses __StackLimit for
         *   optional stack-overflow checking (e.g. MSPLIM on ARMv8-M). */
        __StackLimit = .;
        . = . + STACK_SIZE;
        . = ALIGN(8);
        _estack = .;
        __StackTop = .;
    } > ram

    /* __stack : newlib's _init_signal and some Microchip startup files use
     *   this symbol to initialise the stack pointer.  PROVIDE means it is
     *   only emitted when not already defined by the application. */
    PROVIDE(__stack = __StackTop);

    . = ALIGN(4);
    /* _end : legacy symbol — equivalent to end / __end__; retained for
     *   compatibility with older Microchip/ASF startup and sbrk stubs. */
    _end = . ;
    _ram_end_ = ORIGIN(ram) + LENGTH(ram) - 1 ;

    /*
     * TCM output sections — uncomment and use
     *   __attribute__((section(".itcm_text")))  for code in ITCM
     *   __attribute__((section(".dtcm_data")))  for data in DTCM
     * to place performance-critical code or data in TCM.
     * TCM must be enabled (TCM_Enable() called) before these sections are used.
     */
    /*
    .tcm_text :
    {
        . = ALIGN(4);
        *(.tcm_text .tcm_text.*)
    } > tcm AT > rom
    .tcm_data :
    {
        . = ALIGN(4);
        *(.tcm_data .tcm_data.*)
    } > tcm AT > rom
    */

    /* Device configuration sections */
    /* Use __attribute__((section(".<section_name>"))) to place data in these regions */
    /* Each region has a dedicated PT_LOAD segment (see PHDRS above) so that
       programmed content (e.g. fuses, user row) is preserved in the hex
       output produced by objcopy. */
    .sw0 :
    {
        KEEP(*(.sw0 .sw0.*))
    } > sw0 :sw0
    .temp_log :
    {
        KEEP(*(.temp_log .temp_log.*))
    } > temp_log :temp_log
    .user_page :
    {
        KEEP(*(.user_page .user_page.*))
    } > user_page :user_page
}

/* Verify the combined .relocate + .tdata flash image size matches its RAM
 * footprint, and that RAM usage does not exceed available RAM. These
 * checks are placed here, completely outside the SECTIONS block, because
 * placing an ASSERT() command directly before (or even at the tail end of)
 * a SECTIONS block containing empty NOLOAD/config sections such as .tbss
 * confuses lld's linker-script parser, causing spurious
 * "symbol not found: .tbss" / syntax errors. Top-level ASSERT commands
 * after the SECTIONS block evaluate cleanly with both lld and GNU ld. */
ASSERT(__data_size == __data_source_size,
       "ERROR: .relocate/.tdata flash size does not match RAM size");

ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed with stack");
