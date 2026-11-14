/*
 * GCC startup file for ATSAME51G18A
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
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

#include <stdint.h>
#include <stdbool.h>

#include "same51g18a.h"

/* Initialize segments */
extern uint32_t _sfixed;
extern uint32_t _efixed;
extern uint32_t _etext;
extern uint32_t _srelocate;
extern uint32_t _erelocate;
extern uint32_t _szero;
extern uint32_t _ezero;
extern uint32_t _sstack;
extern uint32_t _estack;

/* Optional application-provided functions */
extern void __attribute__((weak)) _on_reset(void);
extern void __attribute__((weak)) _on_bootstrap(void);
extern void __attribute__((weak)) _on_exit(void);


/*
 * =============================================================================
 * TCM (Tightly Coupled Memory) Initialization — GCC / IAR / ARMCC
 * =============================================================================
 * Provides TCM_Enable() and TCM_Disable() using the standard CMSIS SCB
 * registers SCB->ITCMCR / SCB->DTCMCR.  These registers are present on all
 * Cortex-M7 devices that have ITCM and/or DTCM.  The guard below uses the
 * CMSIS __ITCM_PRESENT / __DTCM_PRESENT feature macros so this file compiles
 * out completely on cores without TCM.
 *
 * ORDERING:
 *   TCM_Enable() MUST run in Reset_Handler BEFORE the .data copy and .bss
 *   zero, because linker sections (e.g. .text_tcm) may be placed in ITCM or
 *   DTCM and the first write to those regions must be to enabled RAM.
 *
 * CONTROL BITS (written together in TCM_Enable):
 *   EN    — brings the TCM online at its architecturally-defined address.
 *   RMW   — "Read-Modify-Write" for narrower-than-word stores.  With RMW set,
 *           byte and half-word writes go through the hardware RMW path so
 *           they behave like normal byte/half-word accesses; without RMW,
 *           narrower stores cannot be performed on ECC-protected TCM because
 *           the syndrome covers the whole word.
 *   RETEN — "Retention".  Holds TCM contents (and their ECC syndromes, where
 *           applicable) across low-power / sleep transitions instead of
 *           powering the RAM off.  Without RETEN, the TCM wakes up
 *           uninitialised and would need to be re-seeded.
 *
 * User override:
 *   Define __STARTUP_TCM_DISABLE on the compiler command line to skip the
 *   TCM_Enable() call in Reset_Handler entirely (e.g. for bootloaders that
 *   must not claim the TCM).
 *
 * MISRA C:2012: the write to SCB->ITCMCR / SCB->DTCMCR combines bit masks
 * from the CMSIS core header; the left-hand side is volatile so the
 * assignment is an I/O access in the MISRA sense.
 */

#if (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1)

__STATIC_INLINE void TCM_Enable(void)
{
    __DSB();
    __ISB();
#if (__ITCM_PRESENT == 1)
    SCB->ITCMCR = (SCB_ITCMCR_EN_Msk | SCB_ITCMCR_RMW_Msk | SCB_ITCMCR_RETEN_Msk);
#endif
#if (__DTCM_PRESENT == 1)
    SCB->DTCMCR = (SCB_DTCMCR_EN_Msk | SCB_DTCMCR_RMW_Msk | SCB_DTCMCR_RETEN_Msk);
#endif
    __DSB();
    __ISB();
}

__STATIC_INLINE void TCM_Disable(void)
{
    __DSB();
    __ISB();
#if (__ITCM_PRESENT == 1)
    SCB->ITCMCR &= ~(uint32_t)SCB_ITCMCR_EN_Msk;
#endif
#if (__DTCM_PRESENT == 1)
    SCB->DTCMCR &= ~(uint32_t)SCB_DTCMCR_EN_Msk;
#endif
    __DSB();
    __ISB();
}

#endif /* (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1) */

/*
 * =============================================================================
 * CMCC (Cortex-M Cache Controller) Configuration
 * =============================================================================
 * Configures the hardware cache controller present on a number of Cortex-M
 * devices where a fixed-size on-chip SRAM is shared between the cache and
 * the TCM: the larger the TCM, the smaller the cache, and vice-versa.  This
 * file picks the cache size that matches the TCM length the linker has been
 * told to reserve.
 *
 * ALGORITHM (per CMCC instance):
 *   1. If CMCC_SR.CSTS is already set, the cache is enabled — leave the
 *      current configuration in place (this short-circuits warm-reset paths
 *      where the first-boot code has already configured the cache).  On
 *      dual-CMCC devices each instance is wrapped in its own `do { ... }
 *      while (0)` block so that a warm CMCC0 does not skip CMCC1.
 *   2. Clear CMCC_CTRL.CEN to place the cache in a known disabled state; a
 *      disabled cache is required before CMCC_CFG may be reprogrammed.
 *   3. Select a cache size via CMCC_CFG.CSIZESW, derived at compile time
 *      from __STARTUP_<tcm>_LENGTH (the linker script exposes this as 0
 *      when no TCM is reserved, so by default the full SRAM becomes cache).
 *   4. Re-enable the cache by writing CMCC_CTRL.CEN, unless the TCM length
 *      consumes the entire SRAM (in which case the cache is intentionally
 *      left disabled).
 *
 * LENGTH MACROS (from the linker script, default 0):
 *   __STARTUP_TCM_LENGTH    — single-CMCC devices
 *   __STARTUP_ITCM_LENGTH   — dual-CMCC devices (CMCC0 side, ITCM)
 *   __STARTUP_DTCM_LENGTH   — dual-CMCC devices (CMCC1 side, DTCM)
 * Override these on the compiler command line or in the linker script to
 * split the SRAM between cache and TCM.
 *
 * CALL SITE:
 *   CMCC_Configure() is called from SystemInit(), which runs after the
 *   core-level initialisation in Reset_Handler.  The cache is therefore
 *   available from the start of main().
 *
 * REGISTER-LEVEL COMPATIBILITY:
 *   CMCC v6 names the config bit-field CMCC_CFG_CSIZESW; CMCC v7 renames
 *   the same field to CMCC_CFG_PRGCSIZE.  The shim below aliases the v7
 *   macro onto the v6 name so the rest of the file uses one form.
 *
 * MISRA C:2012:
 *   - Rule 11.3 / 11.6: pointers to the CMCC register block come from the
 *     device header (e.g. CMCC_REGS) and are treated here as opaque MMIO.
 *   - Rule 15.5 (single point of exit): the per-instance `do { ... } while
 *     (0)` block uses `break` (not `return`) to short-circuit the
 *     "cache already on, leave it alone" case, so that on dual-CMCC devices
 *     the second instance is still configured when the first is already on.
 */

/* Compatibility shim: CMCC v6 uses CMCC_CFG_CSIZESW, v7 uses CMCC_CFG_PRGCSIZE */
#if !defined(CMCC_CFG_CSIZESW) && defined(CMCC_CFG_PRGCSIZE)
#define CMCC_CFG_CSIZESW CMCC_CFG_PRGCSIZE
#endif

#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val _UINT32_(0x0)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val _UINT32_(0x1)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val _UINT32_(0x2)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_8KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_8KB_Val _UINT32_(0x3)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_16KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_16KB_Val _UINT32_(0x4)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_32KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_32KB_Val _UINT32_(0x5)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_64KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_64KB_Val _UINT32_(0x6)
#endif

__STATIC_INLINE void CMCC_Configure(void)
{

#if defined(CMCC_REGS) && defined(CMCC_SR_CSTS_Msk)
    /* Per-instance `do { ... } while (0)` so that a warm cache on one CMCC
     * instance does not short-circuit configuration of any sibling instance.
     * MISRA-C:2012 Rule 15.5 deviation: `break` is used as a single early
     * exit to keep the body flat instead of nesting the remainder in an
     * else-branch. */
    do {
    /* If the cache is already enabled (warm reset / pre-configured by a
     * bootloader), leave the existing configuration untouched. */
    if (CMCC_REGS->CMCC_SR & CMCC_SR_CSTS_Msk) {
        break;
    }

    /* Disable the cache so that CMCC_CFG may be reprogrammed.  Per the CMCC
     * DOS, CMCC_CFG is read-only while the cache is enabled. */
    CMCC_REGS->CMCC_CTRL &= ~(CMCC_CTRL_CEN_Msk);

    /* Select the cache size based on the TCM length the linker script has
     * reserved.  The chain below expands to:
     *
     *     #if   __STARTUP_<tcm>_LENGTH == <size_0>  -> cache = MAX - <size_0>
     *     #elif __STARTUP_<tcm>_LENGTH == <size_1>  -> cache = MAX - <size_1>
     *     ...
     *     #elif __STARTUP_<tcm>_LENGTH == <size_N>  -> (all SRAM used as TCM;
     *                                                   no cache configured,
     *                                                   CEN remains cleared)
     *     #else                                     -> __STARTUP_<tcm>_LENGTH
     *                                                   is 0, undefined, or
     *                                                   not one of the listed
     *                                                   sizes -> default to
     *                                                   the maximum cache. */
#if (__STARTUP_TCM_LENGTH == 0x800)
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#elif (__STARTUP_TCM_LENGTH == 0xC00)
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#elif (__STARTUP_TCM_LENGTH == 0x1000)
    /* TCM consumes all of the shared SRAM -- no room for cache, leave CEN cleared. */
#else
    /* __STARTUP_TCM_LENGTH is 0, undefined, or not one of the listed
     * sizes -- default to the maximum cache size (no TCM reserved). */
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#endif
    } while (0);

#else /* CMCC_REGS */
    #warning "Failed to configure CMCC for this device"
#endif /* CMCC_REGS */

}

/** \cond DOXYGEN_SHOULD_SKIP_THIS */
int main(void);
/** \endcond */

/**
 * \brief Initialize the relocate (.data) segment and clear the zero (.bss)
 * segment using inline assembly that performs explicit 32-bit word
 * (ldr/str) accesses.
 *
 * This is written in assembly on purpose: a hand-written C loop can be
 * recognized by the compiler's loop-idiom optimizer
 * (-ftree-loop-distribute-patterns) and rewritten into a call to
 * memcpy()/memset(), which may use 8-bit accesses. Some init memory
 * (e.g. ECC-protected RAM) only tolerates 32-bit accesses, so the copy
 * must stay word-sized at every optimization level. Inline asm cannot
 * be turned into a libcall, so this holds for -O0..-O3/-Os and LTO
 * without relying on any build flag or function attribute.
 *
 * The sequence avoids post-indexed ldr/str so that a single block works
 * on every Cortex-M variant, including ARMv6-M (Cortex-M0/M0+/M23).
 */
static inline void startup_memory_init(void)
{
        /*
         * Initialize the relocate (.data) segment and clear the zero (.bss)
         * segment using inline assembly that performs explicit 32-bit word
         * (ldr/str) accesses.
         *
         * This is written in assembly on purpose: a hand-written C loop can be
         * recognized by the compiler's loop-idiom optimizer
         * (-ftree-loop-distribute-patterns) and rewritten into a call to
         * memcpy()/memset(), which may use 8-bit accesses. Some init memory
         * (e.g. ECC-protected RAM) only tolerates 32-bit accesses, so the copy
         * must stay word-sized at every optimization level. Inline asm cannot
         * be turned into a libcall, so this holds for -O0..-O3/-Os and LTO
         * without relying on any build flag or function attribute.
         *
         * The sequence avoids post-indexed ldr/str so that a single block works
         * on every Cortex-M variant, including ARMv6-M (Cortex-M0/M0+/M23).
         */
        __asm__ volatile (
                /*
                 * Force unified assembler syntax: the compiler emits inline asm
                 * under '.syntax divided', where the three-operand 'adds rd, rn,
                 * #imm' form is rejected as "not supported in Thumb16 mode" on
                 * ARMv6-M (Cortex-M0/M0+/M23). Unified syntax is valid on every
                 * Cortex-M core and the compiler restores '.syntax unified' after
                 * this block, so no restore is needed here.
                 */
                ".syntax unified       \n"
                /* Copy the relocate (.data) segment as 32-bit words. */
                "ldr  r0, =_etext      \n" /* source start            */
                "ldr  r1, =_srelocate  \n" /* destination start       */
                "ldr  r2, =_erelocate  \n" /* destination end         */
                "cmp  r0, r1           \n"
                "beq  2f               \n" /* skip copy if src == dst */
                "1:                    \n"
                "cmp  r1, r2           \n"
                "bhs  2f               \n"
                "ldr  r3, [r0]         \n"
                "str  r3, [r1]         \n"
                "adds r0, r0, #4       \n"
                "adds r1, r1, #4       \n"
                "b    1b               \n"
                "2:                    \n"
                /* Clear the zero (.bss) segment as 32-bit words. */
                "ldr  r1, =_szero      \n" /* destination start       */
                "ldr  r2, =_ezero      \n" /* destination end         */
                "movs r3, #0           \n"
                "3:                    \n"
                "cmp  r1, r2           \n"
                "bhs  4f               \n"
                "str  r3, [r1]         \n"
                "adds r1, r1, #4       \n"
                "b    3b               \n"
                "4:                    \n"
                ::: "r0", "r1", "r2", "r3", "cc", "memory"
        );
}

void __libc_init_array(void);

/* Reset handler */
void Reset_Handler(void);

/* Default empty handler */
void Default_Handler(void);

/* Cortex-M4 core handlers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void NonMaskableInt_Handler         (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void NMI_Handler                    (void);
void MemoryManagement_Handler       (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void MemManage_Handler              (void);
void SVCall_Handler                 (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void SVC_Handler                    (void);
void DebugMonitor_Handler           (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void DebugMon_Handler               (void);

/* Trampoline functions for backward compatibility */
void __attribute__ ((weak)) NMI_Handler(void) {
    NonMaskableInt_Handler();
}
void __attribute__ ((weak)) MemManage_Handler(void) {
    MemoryManagement_Handler();
}
void __attribute__ ((weak)) SVC_Handler(void) {
    SVCall_Handler();
}
void __attribute__ ((weak)) DebugMon_Handler(void) {
    DebugMonitor_Handler();
}
#pragma GCC diagnostic pop /* -Wdeprecated-declarations */

void HardFault_Handler              (void) __attribute__ ((weak, alias("Default_Handler")));
void BusFault_Handler               (void) __attribute__ ((weak, alias("Default_Handler")));
void UsageFault_Handler             (void) __attribute__ ((weak, alias("Default_Handler")));
void PendSV_Handler                 (void) __attribute__ ((weak, alias("Default_Handler")));
void SysTick_Handler                (void) __attribute__ ((weak, alias("Default_Handler")));

/* Peripherals handlers */
void PM_Handler                     ( void ) __attribute__ ((weak, alias("Default_Handler")));
void MCLK_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_XOSC0_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_XOSC1_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_DFLL_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_DPLL0_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_DPLL1_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSC32KCTRL_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SUPC_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SUPC_BODDET_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void WDT_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void RTC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_0_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_1_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_2_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_3_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_4_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_5_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_6_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_7_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_8_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_9_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_10_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_11_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_12_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_13_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_14_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_15_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void FREQM_Handler                  ( void ) __attribute__ ((weak, alias("Default_Handler")));
void NVMCTRL_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void NVMCTRL_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMAC_0_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMAC_1_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMAC_2_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMAC_3_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMAC_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_0_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_1_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_2_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_3_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_OTHER_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PAC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void RAMECC_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_OTHER_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void CAN0_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_OTHER_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_SOF_HSOF_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_TRCPT0_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_TRCPT1_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC4_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC5_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TC0_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TC1_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TC2_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TC3_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PDEC_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PDEC_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PDEC_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC0_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC0_RESRDY_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC1_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC1_RESRDY_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void AC_Handler                     ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DAC_OTHER_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DAC_EMPTY_0_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DAC_EMPTY_1_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DAC_RESRDY_0_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DAC_RESRDY_1_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PCC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void AES_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TRNG_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ICM_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PUKCC_Handler                  ( void ) __attribute__ ((weak, alias("Default_Handler")));
void QSPI_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SDHC0_Handler                  ( void ) __attribute__ ((weak, alias("Default_Handler")));

/* Exception Table */
__attribute__ ((section(".vectors")))
const DeviceVectors exception_table = {

        /* Configure Initial Stack Pointer, using linker-generated symbols */
        .pvStack = (void*) (&_estack),



        .pfnReset_Handler              = (void*) Reset_Handler,
        .pfnNMI_Handler                = (void*) NMI_Handler,
        .pfnHardFault_Handler          = (void*) HardFault_Handler,
        .pfnMemManage_Handler          = (void*) MemManage_Handler,
        .pfnBusFault_Handler           = (void*) BusFault_Handler,
        .pfnUsageFault_Handler         = (void*) UsageFault_Handler,
        .pvReservedC9                  = (void*) (0UL), /* Reserved */
        .pvReservedC8                  = (void*) (0UL), /* Reserved */
        .pvReservedC7                  = (void*) (0UL), /* Reserved */
        .pvReservedC6                  = (void*) (0UL), /* Reserved */
        .pfnSVC_Handler                = (void*) SVC_Handler,
        .pfnDebugMon_Handler           = (void*) DebugMon_Handler,
        .pvReservedC3                  = (void*) (0UL), /* Reserved */
        .pfnPendSV_Handler             = (void*) PendSV_Handler,
        .pfnSysTick_Handler            = (void*) SysTick_Handler,

        /* Configurable interrupts */
        .pfnPM_Handler                 = (void*) PM_Handler,     /* 0  Power Manager */
        .pfnMCLK_Handler               = (void*) MCLK_Handler,   /* 1  Main Clock */
        .pfnOSCCTRL_XOSC0_Handler      = (void*) OSCCTRL_XOSC0_Handler, /* 2  Oscillators Control */
        .pfnOSCCTRL_XOSC1_Handler      = (void*) OSCCTRL_XOSC1_Handler, /* 3  Oscillators Control */
        .pfnOSCCTRL_DFLL_Handler       = (void*) OSCCTRL_DFLL_Handler, /* 4  Oscillators Control */
        .pfnOSCCTRL_DPLL0_Handler      = (void*) OSCCTRL_DPLL0_Handler, /* 5  Oscillators Control */
        .pfnOSCCTRL_DPLL1_Handler      = (void*) OSCCTRL_DPLL1_Handler, /* 6  Oscillators Control */
        .pfnOSC32KCTRL_Handler         = (void*) OSC32KCTRL_Handler, /* 7  32kHz Oscillators Control */
        .pfnSUPC_OTHER_Handler         = (void*) SUPC_OTHER_Handler, /* 8  Supply Controller */
        .pfnSUPC_BODDET_Handler        = (void*) SUPC_BODDET_Handler, /* 9  Supply Controller */
        .pfnWDT_Handler                = (void*) WDT_Handler,    /* 10 Watchdog Timer */
        .pfnRTC_Handler                = (void*) RTC_Handler,    /* 11 Real-Time Counter */
        .pfnEIC_EXTINT_0_Handler       = (void*) EIC_EXTINT_0_Handler, /* 12 External Interrupt Controller */
        .pfnEIC_EXTINT_1_Handler       = (void*) EIC_EXTINT_1_Handler, /* 13 External Interrupt Controller */
        .pfnEIC_EXTINT_2_Handler       = (void*) EIC_EXTINT_2_Handler, /* 14 External Interrupt Controller */
        .pfnEIC_EXTINT_3_Handler       = (void*) EIC_EXTINT_3_Handler, /* 15 External Interrupt Controller */
        .pfnEIC_EXTINT_4_Handler       = (void*) EIC_EXTINT_4_Handler, /* 16 External Interrupt Controller */
        .pfnEIC_EXTINT_5_Handler       = (void*) EIC_EXTINT_5_Handler, /* 17 External Interrupt Controller */
        .pfnEIC_EXTINT_6_Handler       = (void*) EIC_EXTINT_6_Handler, /* 18 External Interrupt Controller */
        .pfnEIC_EXTINT_7_Handler       = (void*) EIC_EXTINT_7_Handler, /* 19 External Interrupt Controller */
        .pfnEIC_EXTINT_8_Handler       = (void*) EIC_EXTINT_8_Handler, /* 20 External Interrupt Controller */
        .pfnEIC_EXTINT_9_Handler       = (void*) EIC_EXTINT_9_Handler, /* 21 External Interrupt Controller */
        .pfnEIC_EXTINT_10_Handler      = (void*) EIC_EXTINT_10_Handler, /* 22 External Interrupt Controller */
        .pfnEIC_EXTINT_11_Handler      = (void*) EIC_EXTINT_11_Handler, /* 23 External Interrupt Controller */
        .pfnEIC_EXTINT_12_Handler      = (void*) EIC_EXTINT_12_Handler, /* 24 External Interrupt Controller */
        .pfnEIC_EXTINT_13_Handler      = (void*) EIC_EXTINT_13_Handler, /* 25 External Interrupt Controller */
        .pfnEIC_EXTINT_14_Handler      = (void*) EIC_EXTINT_14_Handler, /* 26 External Interrupt Controller */
        .pfnEIC_EXTINT_15_Handler      = (void*) EIC_EXTINT_15_Handler, /* 27 External Interrupt Controller */
        .pfnFREQM_Handler              = (void*) FREQM_Handler,  /* 28 Frequency Meter */
        .pfnNVMCTRL_0_Handler          = (void*) NVMCTRL_0_Handler, /* 29 Non-Volatile Memory Controller */
        .pfnNVMCTRL_1_Handler          = (void*) NVMCTRL_1_Handler, /* 30 Non-Volatile Memory Controller */
        .pfnDMAC_0_Handler             = (void*) DMAC_0_Handler, /* 31 Direct Memory Access Controller */
        .pfnDMAC_1_Handler             = (void*) DMAC_1_Handler, /* 32 Direct Memory Access Controller */
        .pfnDMAC_2_Handler             = (void*) DMAC_2_Handler, /* 33 Direct Memory Access Controller */
        .pfnDMAC_3_Handler             = (void*) DMAC_3_Handler, /* 34 Direct Memory Access Controller */
        .pfnDMAC_OTHER_Handler         = (void*) DMAC_OTHER_Handler, /* 35 Direct Memory Access Controller */
        .pfnEVSYS_0_Handler            = (void*) EVSYS_0_Handler, /* 36 Event System Interface */
        .pfnEVSYS_1_Handler            = (void*) EVSYS_1_Handler, /* 37 Event System Interface */
        .pfnEVSYS_2_Handler            = (void*) EVSYS_2_Handler, /* 38 Event System Interface */
        .pfnEVSYS_3_Handler            = (void*) EVSYS_3_Handler, /* 39 Event System Interface */
        .pfnEVSYS_OTHER_Handler        = (void*) EVSYS_OTHER_Handler, /* 40 Event System Interface */
        .pfnPAC_Handler                = (void*) PAC_Handler,    /* 41 Peripheral Access Controller */
        .pvReserved42                  = (void*) (0UL),          /* 42 Reserved */
        .pvReserved43                  = (void*) (0UL),          /* 43 Reserved */
        .pvReserved44                  = (void*) (0UL),          /* 44 Reserved */
        .pfnRAMECC_Handler             = (void*) RAMECC_Handler, /* 45 RAM ECC */
        .pfnSERCOM0_0_Handler          = (void*) SERCOM0_0_Handler, /* 46 Serial Communication Interface */
        .pfnSERCOM0_1_Handler          = (void*) SERCOM0_1_Handler, /* 47 Serial Communication Interface */
        .pfnSERCOM0_2_Handler          = (void*) SERCOM0_2_Handler, /* 48 Serial Communication Interface */
        .pfnSERCOM0_OTHER_Handler      = (void*) SERCOM0_OTHER_Handler, /* 49 Serial Communication Interface */
        .pfnSERCOM1_0_Handler          = (void*) SERCOM1_0_Handler, /* 50 Serial Communication Interface */
        .pfnSERCOM1_1_Handler          = (void*) SERCOM1_1_Handler, /* 51 Serial Communication Interface */
        .pfnSERCOM1_2_Handler          = (void*) SERCOM1_2_Handler, /* 52 Serial Communication Interface */
        .pfnSERCOM1_OTHER_Handler      = (void*) SERCOM1_OTHER_Handler, /* 53 Serial Communication Interface */
        .pfnSERCOM2_0_Handler          = (void*) SERCOM2_0_Handler, /* 54 Serial Communication Interface */
        .pfnSERCOM2_1_Handler          = (void*) SERCOM2_1_Handler, /* 55 Serial Communication Interface */
        .pfnSERCOM2_2_Handler          = (void*) SERCOM2_2_Handler, /* 56 Serial Communication Interface */
        .pfnSERCOM2_OTHER_Handler      = (void*) SERCOM2_OTHER_Handler, /* 57 Serial Communication Interface */
        .pfnSERCOM3_0_Handler          = (void*) SERCOM3_0_Handler, /* 58 Serial Communication Interface */
        .pfnSERCOM3_1_Handler          = (void*) SERCOM3_1_Handler, /* 59 Serial Communication Interface */
        .pfnSERCOM3_2_Handler          = (void*) SERCOM3_2_Handler, /* 60 Serial Communication Interface */
        .pfnSERCOM3_OTHER_Handler      = (void*) SERCOM3_OTHER_Handler, /* 61 Serial Communication Interface */
        .pfnSERCOM4_0_Handler          = (void*) SERCOM4_0_Handler, /* 62 Serial Communication Interface */
        .pfnSERCOM4_1_Handler          = (void*) SERCOM4_1_Handler, /* 63 Serial Communication Interface */
        .pfnSERCOM4_2_Handler          = (void*) SERCOM4_2_Handler, /* 64 Serial Communication Interface */
        .pfnSERCOM4_OTHER_Handler      = (void*) SERCOM4_OTHER_Handler, /* 65 Serial Communication Interface */
        .pfnSERCOM5_0_Handler          = (void*) SERCOM5_0_Handler, /* 66 Serial Communication Interface */
        .pfnSERCOM5_1_Handler          = (void*) SERCOM5_1_Handler, /* 67 Serial Communication Interface */
        .pfnSERCOM5_2_Handler          = (void*) SERCOM5_2_Handler, /* 68 Serial Communication Interface */
        .pfnSERCOM5_OTHER_Handler      = (void*) SERCOM5_OTHER_Handler, /* 69 Serial Communication Interface */
        .pvReserved70                  = (void*) (0UL),          /* 70 Reserved */
        .pvReserved71                  = (void*) (0UL),          /* 71 Reserved */
        .pvReserved72                  = (void*) (0UL),          /* 72 Reserved */
        .pvReserved73                  = (void*) (0UL),          /* 73 Reserved */
        .pvReserved74                  = (void*) (0UL),          /* 74 Reserved */
        .pvReserved75                  = (void*) (0UL),          /* 75 Reserved */
        .pvReserved76                  = (void*) (0UL),          /* 76 Reserved */
        .pvReserved77                  = (void*) (0UL),          /* 77 Reserved */
        .pfnCAN0_Handler               = (void*) CAN0_Handler,   /* 78 Control Area Network */
        .pvReserved79                  = (void*) (0UL),          /* 79 Reserved */
        .pfnUSB_OTHER_Handler          = (void*) USB_OTHER_Handler, /* 80 Universal Serial Bus */
        .pfnUSB_SOF_HSOF_Handler       = (void*) USB_SOF_HSOF_Handler, /* 81 Universal Serial Bus */
        .pfnUSB_TRCPT0_Handler         = (void*) USB_TRCPT0_Handler, /* 82 Universal Serial Bus */
        .pfnUSB_TRCPT1_Handler         = (void*) USB_TRCPT1_Handler, /* 83 Universal Serial Bus */
        .pvReserved84                  = (void*) (0UL),          /* 84 Reserved */
        .pfnTCC0_OTHER_Handler         = (void*) TCC0_OTHER_Handler, /* 85 Timer Counter Control */
        .pfnTCC0_MC0_Handler           = (void*) TCC0_MC0_Handler, /* 86 Timer Counter Control */
        .pfnTCC0_MC1_Handler           = (void*) TCC0_MC1_Handler, /* 87 Timer Counter Control */
        .pfnTCC0_MC2_Handler           = (void*) TCC0_MC2_Handler, /* 88 Timer Counter Control */
        .pfnTCC0_MC3_Handler           = (void*) TCC0_MC3_Handler, /* 89 Timer Counter Control */
        .pfnTCC0_MC4_Handler           = (void*) TCC0_MC4_Handler, /* 90 Timer Counter Control */
        .pfnTCC0_MC5_Handler           = (void*) TCC0_MC5_Handler, /* 91 Timer Counter Control */
        .pfnTCC1_OTHER_Handler         = (void*) TCC1_OTHER_Handler, /* 92 Timer Counter Control */
        .pfnTCC1_MC0_Handler           = (void*) TCC1_MC0_Handler, /* 93 Timer Counter Control */
        .pfnTCC1_MC1_Handler           = (void*) TCC1_MC1_Handler, /* 94 Timer Counter Control */
        .pfnTCC1_MC2_Handler           = (void*) TCC1_MC2_Handler, /* 95 Timer Counter Control */
        .pfnTCC1_MC3_Handler           = (void*) TCC1_MC3_Handler, /* 96 Timer Counter Control */
        .pfnTCC2_OTHER_Handler         = (void*) TCC2_OTHER_Handler, /* 97 Timer Counter Control */
        .pfnTCC2_MC0_Handler           = (void*) TCC2_MC0_Handler, /* 98 Timer Counter Control */
        .pfnTCC2_MC1_Handler           = (void*) TCC2_MC1_Handler, /* 99 Timer Counter Control */
        .pfnTCC2_MC2_Handler           = (void*) TCC2_MC2_Handler, /* 100 Timer Counter Control */
        .pvReserved101                 = (void*) (0UL),          /* 101 Reserved */
        .pvReserved102                 = (void*) (0UL),          /* 102 Reserved */
        .pvReserved103                 = (void*) (0UL),          /* 103 Reserved */
        .pvReserved104                 = (void*) (0UL),          /* 104 Reserved */
        .pvReserved105                 = (void*) (0UL),          /* 105 Reserved */
        .pvReserved106                 = (void*) (0UL),          /* 106 Reserved */
        .pfnTC0_Handler                = (void*) TC0_Handler,    /* 107 Basic Timer Counter */
        .pfnTC1_Handler                = (void*) TC1_Handler,    /* 108 Basic Timer Counter */
        .pfnTC2_Handler                = (void*) TC2_Handler,    /* 109 Basic Timer Counter */
        .pfnTC3_Handler                = (void*) TC3_Handler,    /* 110 Basic Timer Counter */
        .pvReserved111                 = (void*) (0UL),          /* 111 Reserved */
        .pvReserved112                 = (void*) (0UL),          /* 112 Reserved */
        .pvReserved113                 = (void*) (0UL),          /* 113 Reserved */
        .pvReserved114                 = (void*) (0UL),          /* 114 Reserved */
        .pfnPDEC_OTHER_Handler         = (void*) PDEC_OTHER_Handler, /* 115 Quadrature Decodeur */
        .pfnPDEC_MC0_Handler           = (void*) PDEC_MC0_Handler, /* 116 Quadrature Decodeur */
        .pfnPDEC_MC1_Handler           = (void*) PDEC_MC1_Handler, /* 117 Quadrature Decodeur */
        .pfnADC0_OTHER_Handler         = (void*) ADC0_OTHER_Handler, /* 118 Analog Digital Converter */
        .pfnADC0_RESRDY_Handler        = (void*) ADC0_RESRDY_Handler, /* 119 Analog Digital Converter */
        .pfnADC1_OTHER_Handler         = (void*) ADC1_OTHER_Handler, /* 120 Analog Digital Converter */
        .pfnADC1_RESRDY_Handler        = (void*) ADC1_RESRDY_Handler, /* 121 Analog Digital Converter */
        .pfnAC_Handler                 = (void*) AC_Handler,     /* 122 Analog Comparators */
        .pfnDAC_OTHER_Handler          = (void*) DAC_OTHER_Handler, /* 123 Digital-to-Analog Converter */
        .pfnDAC_EMPTY_0_Handler        = (void*) DAC_EMPTY_0_Handler, /* 124 Digital-to-Analog Converter */
        .pfnDAC_EMPTY_1_Handler        = (void*) DAC_EMPTY_1_Handler, /* 125 Digital-to-Analog Converter */
        .pfnDAC_RESRDY_0_Handler       = (void*) DAC_RESRDY_0_Handler, /* 126 Digital-to-Analog Converter */
        .pfnDAC_RESRDY_1_Handler       = (void*) DAC_RESRDY_1_Handler, /* 127 Digital-to-Analog Converter */
        .pvReserved128                 = (void*) (0UL),          /* 128 Reserved */
        .pfnPCC_Handler                = (void*) PCC_Handler,    /* 129 Parallel Capture Controller */
        .pfnAES_Handler                = (void*) AES_Handler,    /* 130 Advanced Encryption Standard */
        .pfnTRNG_Handler               = (void*) TRNG_Handler,   /* 131 True Random Generator */
        .pfnICM_Handler                = (void*) ICM_Handler,    /* 132 Integrity Check Monitor */
        .pfnPUKCC_Handler              = (void*) PUKCC_Handler,  /* 133 PUblic-Key Cryptography Controller */
        .pfnQSPI_Handler               = (void*) QSPI_Handler,   /* 134 Quad SPI interface */
        .pfnSDHC0_Handler              = (void*) SDHC0_Handler   /* 135 SD/MMC Host Controller */
};

/**
 * \brief This is the code that gets called on processor reset.
 * To initialize the device, and call the main() routine.
 */
void Reset_Handler(void)
{
#if (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1)
        /* Enable TCM before .data copy — linker may place sections in ITCM/DTCM */
#if !defined(__STARTUP_TCM_DISABLE)
        TCM_Enable();
#endif
#endif /* (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1) */

        /* ECC RAM must be initialised before .bss zero-fill by C startup */
#if defined(TCMECC_REGS) && defined(TCMECC_CR_ENABLE_Msk)
        TCMECC_Initialize();
        FLEXRAM_EccInitialize();
#elif defined(DRMTCM_REGS)
        DRMTCM_RamInitialize();
#elif defined(MCRAMC_REGS) || defined(MCRAMC)
        MCRAMC_EccInitialize();
#endif

        /* Initialize .data and .bss using an assembly helper (MISRA Dir 4.3). */
        startup_memory_init();

#ifdef SCB_VTOR_TBLOFF_Msk
        /* Set the vector table base address */
        const uint32_t *pSrc = (uint32_t *) & _sfixed;
        SCB->VTOR = ((uint32_t) pSrc & SCB_VTOR_TBLOFF_Msk);
#endif

        /* Call the optional application-provided _on_reset() function. */
        if ((bool)_on_reset) {
                _on_reset();
        }

        /* Initialize the C library */
        __libc_init_array();

        /* Call the optional application-provided _on_bootstrap() function. */
        if ((bool)_on_bootstrap) {
                _on_bootstrap();
        }

        /* Branch to main function */
        (void)main();

        /* Call the optional application-provided _on_exit() function. */
        if ((bool)_on_exit) {
                _on_exit();
        } else {
                /* Infinite loop */
                while (true) {}
        }
}

/**
 * \brief Default interrupt handler for unused IRQs.
 */
void Default_Handler(void)
{
        while (true) {}
}
