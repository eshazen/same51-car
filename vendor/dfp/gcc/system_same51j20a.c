/*
 * System configuration file for ATSAME51J20A
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

#include <stdbool.h>
#include <stdint.h>
#include "same51j20a.h"
#include "system_same51j20a.h"

/*----------------------------------------------------------------------------
  Define clocks
 *----------------------------------------------------------------------------*/
#define SYSTEM_CLOCK    (48000000UL)

/*----------------------------------------------------------------------------
  System Core Clock Variable
 *----------------------------------------------------------------------------*/
uint32_t SystemCoreClock = (uint32_t)SYSTEM_CLOCK;  /* System Core Clock Frequency */


/*----------------------------------------------------------------------------
  System Core Clock update function
 *----------------------------------------------------------------------------*/
#if defined(__ICCARM__)
#pragma weak SystemCoreClockUpdate_PreHook
#else
__attribute__((weak))
#endif
bool SystemCoreClockUpdate_PreHook(void)
{
    // Default implementation: Do nothing.
    return false;
}

void SystemCoreClockUpdate (void)
{
    if (SystemCoreClockUpdate_PreHook()) {
        return;
    }

    SystemCoreClock = (uint32_t)SYSTEM_CLOCK;
}


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

/*----------------------------------------------------------------------------
  System initialization function
 *----------------------------------------------------------------------------*/
void SystemInit (void)
{
#if defined (__FPU_USED) && (__FPU_USED == 1U)
    SCB->CPACR |= ((3UL << 10U*2U) |           /* enable CP10 Full Access */
                  (3UL << 11U*2U)  );         /* enable CP11 Full Access */
#endif

#ifdef UNALIGNED_SUPPORT_DISABLE
    SCB->CCR |= SCB_CCR_UNALIGN_TRP_Msk;
#endif

    CMCC_Configure();

    SystemCoreClock = (uint32_t)SYSTEM_CLOCK;
}
