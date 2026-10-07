/* SPDX-License-Identifier: MIT */
#ifndef NTASI_APPLE_NVME_H
#define NTASI_APPLE_NVME_H

/*
 * Apple ANS2/NVMe storage controller contract, host-testable seam.
 *
 * Ported from the MIT-licensed m1n1 driver and the upstream/downstream Asahi
 * Linux apple-nvme driver. ANS is a coprocessor (ASC mailbox + RTKit
 * firmware) fronting the internal boot NVMe controller. T8015-class ANS uses
 * conventional NVMe SQ tail doorbells (with 128-byte I/O SQ entries), while
 * T8103 and every currently supported ANS2/ANS3 compatible use linear SQs
 * plus a proprietary NVMMU with one 128-byte TCB per command tag. Both use
 * Apple's field-modified 64-byte SQEs / 16-byte CQEs (u8 tag instead of u16
 * CID; u64 result instead of DW0/DW1).
 *
 * This header models only the parts of the contract that are pure data:
 * struct layouts, register offsets, and register-value/state-machine
 * arithmetic. No MMIO, no RTKit/ASC transport, no timing, no ADT parsing —
 * those belong to the runtime driver layer. See
 * drivers/AppleNvme/README.md for the full spec this seam implements, and
 * that document's "Open questions" (also mirrored in this seam's README)
 * for what is verified-but-not-yet-tested on real T6050 hardware.
 *
 * Every constant here traces to a specific nvme.c line; see the .c file and
 * README for citations. Deliberately standalone: does not include or depend
 * on apple-sart-core, apple-rtkit-core, or any other sibling seam.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Sizes (nvme.c:16, 119-121, 138-146, 538)                            */
/* ------------------------------------------------------------------ */

#define NTASI_ANS_QUEUE_SLOTS 64u /* NVME_QUEUE_SIZE, nvme.c:16 */
#define NTASI_ANS_SQE_SIZE    64u /* sizeof(struct nvme_command), nvme.c:119 */
#define NTASI_ANS_CQE_SIZE    16u /* sizeof(struct nvme_completion), nvme.c:120 */
#define NTASI_ANS_TCB_SIZE    128u /* sizeof(struct apple_nvmmu_tcb), nvme.c:121 */
#define NTASI_ANS_QUEUE_ALIGN 16384u /* memalign(SZ_16K, ...), nvme.c:138-146 */
#define NTASI_ANS_DATA_ALIGN  4096u /* NVMe page size, nvme.c:538 */

/* ------------------------------------------------------------------ */
/* Register map (offsets relative to the ANS MMIO base, nvme.c:18-56) */
/* ------------------------------------------------------------------ */

/* Standard NVMe registers used (spec-compatible). */
#define NTASI_ANS_REG_CAP     0x00u
#define NTASI_ANS_REG_CC      0x14u /* nvme.c:18 */
#define NTASI_ANS_REG_CSTS    0x1cu /* nvme.c:25 */
#define NTASI_ANS_REG_AQA     0x24u /* nvme.c:32, written nvme.c:366 */
#define NTASI_ANS_REG_ASQ     0x28u /* nvme.c:33, written nvme.c:364 */
#define NTASI_ANS_REG_ACQ     0x30u /* nvme.c:34, written nvme.c:365 */
#define NTASI_ANS_REG_DB_ASQ  0x1000u
#define NTASI_ANS_REG_DB_ACQ  0x1004u /* nvme.c:36, written nvme.c:271 */
#define NTASI_ANS_REG_DB_IOSQ 0x1008u
#define NTASI_ANS_REG_DB_IOCQ 0x100cu /* nvme.c:37, written nvme.c:273 */

/* CC fields (nvme.c:18-23). */
#define NTASI_ANS_CC_EN         (1u << 0)
#define NTASI_ANS_CC_SHN_SHIFT  14u
#define NTASI_ANS_CC_SHN_MASK   (0x3u << NTASI_ANS_CC_SHN_SHIFT) /* GENMASK(15,14) */
#define NTASI_ANS_CC_IOSQES_SHIFT 16u
#define NTASI_ANS_CC_IOSQES_MASK  (0xfu << NTASI_ANS_CC_IOSQES_SHIFT)
#define NTASI_ANS_CC_IOCQES_SHIFT 20u
#define NTASI_ANS_CC_IOCQES_MASK  (0xfu << NTASI_ANS_CC_IOCQES_SHIFT)
#define NTASI_ANS_CC_IOSQES_64    (6u << NTASI_ANS_CC_IOSQES_SHIFT)
#define NTASI_ANS_CC_IOCQES_16    (4u << NTASI_ANS_CC_IOCQES_SHIFT)
#define NTASI_ANS_CC_SHN_NONE   0u
#define NTASI_ANS_CC_SHN_NORMAL 1u
#define NTASI_ANS_CC_SHN_ABRUPT 2u

/* CSTS fields (nvme.c:25-30). */
#define NTASI_ANS_CSTS_RDY         (1u << 0)
#define NTASI_ANS_CSTS_CFS         (1u << 1)
#define NTASI_ANS_CSTS_SHST_SHIFT  2u
#define NTASI_ANS_CSTS_SHST_MASK   (0x3u << NTASI_ANS_CSTS_SHST_SHIFT) /* GENMASK(3,2) */
#define NTASI_ANS_CSTS_SHST_NORMAL 0u
#define NTASI_ANS_CSTS_SHST_BUSY   1u
#define NTASI_ANS_CSTS_SHST_DONE   2u

/* Apple-proprietary registers. */
#define NTASI_ANS_REG_BOOT_STATUS 0x1300u /* nvme.c:39, polled nvme.c:345 */
#define NTASI_ANS_BOOT_STATUS_OK  0xde71ce55u /* nvme.c:40 */

#define NTASI_ANS_REG_MAX_PEND_CMDS 0x1210u /* nvme.c:48, written nvme.c:353-354 */

#define NTASI_ANS_REG_UNKNOWN_CTRL       0x24008u /* nvme.c:45, cleared nvme.c:352 */
#define NTASI_ANS_UNKCTRL_PRP_NULL_CHECK (1u << 11) /* nvme.c:46 */

#define NTASI_ANS_REG_LINEAR_SQ_CTRL 0x24908u /* nvme.c:42, set nvme.c:351 */
#define NTASI_ANS_LINEAR_SQ_EN       (1u << 0) /* nvme.c:43 */

#define NTASI_ANS_REG_DB_LINEAR_ASQ  0x2490cu /* nvme.c:49, written nvme.c:235 */
#define NTASI_ANS_REG_DB_LINEAR_IOSQ 0x24910u /* nvme.c:50, written nvme.c:237 */

#define NTASI_ANS_REG_NVMMU_NUM       0x28100u /* nvme.c:52, written nvme.c:355 */
#define NTASI_ANS_REG_NVMMU_ASQ_BASE  0x28108u /* nvme.c:53, written nvme.c:356 */
#define NTASI_ANS_REG_NVMMU_IOSQ_BASE 0x28110u /* nvme.c:54, written nvme.c:357 */
#define NTASI_ANS_REG_NVMMU_TCB_INVAL 0x28118u /* nvme.c:55, written nvme.c:259 */
#define NTASI_ANS_REG_NVMMU_TCB_STAT 0x28120u /* apple.c:61, read after invalidation */

/* Admin command opcodes (nvme.c:58-62). */
#define NTASI_ANS_ADMIN_CMD_DELETE_SQ 0x00u
#define NTASI_ANS_ADMIN_CMD_CREATE_SQ 0x01u
#define NTASI_ANS_ADMIN_CMD_DELETE_CQ 0x04u
#define NTASI_ANS_ADMIN_CMD_CREATE_CQ 0x05u
#define NTASI_ANS_ADMIN_CMD_IDENTIFY  0x06u
#define NTASI_ANS_QUEUE_CONTIGUOUS    (1u << 0) /* NVME_QUEUE_CONTIGUOUS, nvme.c:62 */
#define NTASI_ANS_QUEUE_CQ_IRQ_ENABLED (1u << 1) /* NVME_CQ_IRQ_ENABLED, apple.c */

/*
 * ps_ans2 PMGR reset word.  Linux's apple-pmgr reset controller performs
 * four ordered RMWs around apple_rtkit_reinit(): disable device, assert
 * reset, deassert reset, enable device.  FLAGS are write-control bits and
 * must be cleared by every RMW; unrelated bits must be preserved.
 */
#define NTASI_ANS_PMGR_RESET        (1u << 31)
#define NTASI_ANS_PMGR_AUTO_ENABLE  (1u << 28)
#define NTASI_ANS_PMGR_PS_RESET     (1u << 12)
/*
 * BIT(11) -- READ-ONLY STATUS, AND THE TWO REFERENCES DISAGREE ON ITS NAME.
 *
 * m1n1 src/pmgr.c:12 calls it PMGR_PARENT_OFF; Linux
 * drivers/pmdomain/apple/pmgr-pwrstate.c calls it APPLE_PMGR_BUSY.  Neither
 * ever writes it.  Under EITHER reading it is the hardware's own answer to
 * "why will this domain not reach PS_ACTUAL == PS_TARGET", which is the one
 * question a power-on timeout cannot answer from the target field alone -- so
 * it is sampled and published raw, and the decoder names both interpretations
 * rather than picking one this project has not verified on silicon.
 */
#define NTASI_ANS_PMGR_PARENT_OFF   (1u << 11)
#define NTASI_ANS_PMGR_DEV_DISABLE  (1u << 10)
#define NTASI_ANS_PMGR_FLAGS        ((1u << 9) | (1u << 8))
#define NTASI_ANS_PMGR_ACTUAL_MASK  (0xfu << 4)
#define NTASI_ANS_PMGR_ACTUAL_ACTIVE (0xfu << 4)
#define NTASI_ANS_PMGR_TARGET_MASK  0xfu
#define NTASI_ANS_PMGR_TARGET_ACTIVE 0xfu

/*
 * "Is this domain usable?", the way the hardware's own OS answers it.
 *
 * PS_ACTUAL == ACTIVE is NOT the whole predicate.  A domain under hardware
 * auto power management sits at whatever PS_AUTO says while idle and is
 * raised on demand, so it can read PS_ACTUAL=0 while being perfectly alive.
 * Linux states this outright in
 * drivers/pmdomain/apple/pmgr-pwrstate.c:apple_pmgr_ps_is_active():
 *
 *   "We consider domains as active if they are actually on, or if they have
 *    auto-PM enabled and the intended target is on."
 *
 * MEASURED on J813 2026-08-14: APCIE_SYS_ST @0x380700520 reads 0x1000030F --
 * AUTO_ENABLE=1, PS_AUTO=0, PS_TARGET=0xF, PS_ACTUAL=0.  Under the narrow
 * predicate the pre-flight called that domain dead and ACPI\NTAS2003 came up
 * CM_PROB_FAILED_START; under this one it is active, which is also what Mu's
 * own ANS DXE proves by completing NVMe bring-up with the word in that state.
 *
 * This stays FAIL-CLOSED for a genuinely powered-off domain: PS_TARGET=0xF
 * alone is not enough, AUTO_ENABLE must be set with it.  It is deliberately
 * NOT the same thing as skipping non-REQUIRED domains, which was tried on
 * 2026-08-14 and measured wrong.
 */
static inline bool ntasi_ans_pmgr_domain_is_active(uint32_t word)
{
    if ((word & NTASI_ANS_PMGR_ACTUAL_MASK) == NTASI_ANS_PMGR_ACTUAL_ACTIVE)
        return true;
    return (word & NTASI_ANS_PMGR_TARGET_MASK) ==
               NTASI_ANS_PMGR_TARGET_ACTIVE &&
           (word & NTASI_ANS_PMGR_AUTO_ENABLE) != 0;
}

enum ntasi_ans_pmgr_reset_status {
    NTASI_ANS_PMGR_RESET_OK = 0,
    NTASI_ANS_PMGR_RESET_ERR_ARGUMENT = -1,
    NTASI_ANS_PMGR_RESET_ERR_NOT_ACTIVE = -2,
    NTASI_ANS_PMGR_RESET_ERR_STALE_CONTROL = -3,
    NTASI_ANS_PMGR_RESET_ERR_DISABLE_TIMEOUT = -4,
    NTASI_ANS_PMGR_RESET_ERR_ASSERT_TIMEOUT = -5,
    NTASI_ANS_PMGR_RESET_ERR_DEASSERT_TIMEOUT = -6,
    NTASI_ANS_PMGR_RESET_ERR_ENABLE_TIMEOUT = -7,
};

enum ntasi_ans_pmgr_power_status {
    NTASI_ANS_PMGR_POWER_OK = 0,
    /*
     * The domain already read ACTUAL==ACTIVE with no stale control bits, so
     * no register write was issued.  Measured on live J414s/T6020 hardware
     * (2026-07-30): iBoot leaves all four ANS domains ACTIVE and m1n1's
     * pmgr_init() never powers anything down, so this is the NORMAL result
     * on this platform.  It is reported distinctly from POWER_OK so
     * telemetry can tell "already on, untouched" from "we powered it up",
     * rather than both looking like an indistinguishable success.
     */
    NTASI_ANS_PMGR_POWER_OK_ALREADY_ACTIVE = 1,
    /*
     * The SoC layout marks this domain ABSENT or ADVISORY, so the sequence
     * stepped over it without reading or writing a register.  Distinct from
     * ALREADY_ACTIVE because "we chose not to touch it" and "we looked and it
     * was on" are different claims, and on T8142 the advisory domain is
     * measurably NOT on.
     */
    NTASI_ANS_PMGR_POWER_OK_SKIPPED = 2,
    NTASI_ANS_PMGR_POWER_ERR_ARGUMENT = -20,
    NTASI_ANS_PMGR_POWER_ERR_TIMEOUT = -21,
};

struct ntasi_ans_pmgr_reset_ops {
    uint32_t (*read32)(void *opaque);
    void (*write32)(void *opaque, uint32_t value);
    void (*stall_us)(void *opaque, uint32_t usec);
};

/*
 * ------------------------------------------------------------------
 * ANS PMGR power-domain dependency chain (ordering is the whole
 * correctness property, so it lives in exactly one place).
 * ------------------------------------------------------------------
 *
 * Names are the ADT spellings (UPPERCASE), which is what Apple's device
 * tree and m1n1 use.  The lowercase ps_* spellings are Linux DEVICETREE
 * names and appear nowhere as data -- this driver never string-matches
 * against ADT/ACPI names at all, it resolves each domain purely by its
 * ACPI _CRS ACCESS_RANGE index as emitted by Mu's AcpiPlatformDxe.
 *
 * Ordering rationale, from Asahi's t602x-pmgr.dtsi parent links:
 *   APCIE_ST_SYS  parents = APCIE_ST, ANS2      -> both must precede it
 *   APCIE_ST1_SYS parent  = APCIE_ST_SYS        -> must follow it
 *   APCIE_ST/ANS2 parent  = AFNC6_LW0, which is apple,always-on and so
 *                           needs no explicit bring-up.
 * Hence: APCIE_ST -> ANS2 -> APCIE_ST_SYS -> APCIE_ST1_SYS.
 */
enum ntasi_ans_pmgr_domain_id {
    NTASI_ANS_PMGR_DOMAIN_APCIE_ST = 0,
    NTASI_ANS_PMGR_DOMAIN_ANS2,
    NTASI_ANS_PMGR_DOMAIN_APCIE_ST_SYS,
    NTASI_ANS_PMGR_DOMAIN_APCIE_ST1_SYS,
    NTASI_ANS_PMGR_DOMAIN_COUNT,
};

/*
 * Offsets within the enclosing PMGR block, from Asahi Linux
 * arch/arm64/boot/dts/apple/t602x-pmgr.dtsi.  All four ANS-relevant
 * power-controller nodes live in the *pmgr_east* section of that file
 * (&DIE_NODE(pmgr_east), base 0x290280000), NOT in the first `pmgr`
 * section (0x28e080000) that appears earlier in the same file.
 *
 * Independently confirmed against live J414s hardware over the m1n1 proxy
 * (2026-07-30): APCIE_ST=0x2902801a0 ANS2=0x2902801a8
 * APCIE_ST_SYS=0x290280408 APCIE_ST1_SYS=0x290280410.
 */
/*
 * SoC FINGERPRINT for the pmgr_east base below.
 *
 * NTASI_ANS_PMGR_T602X_EAST_BASE is T602x-SPECIFIC.  When the four words come
 * from firmware _CRS, ntasi_ans_pmgr_validate_layout() is what stops a
 * different SoC's addresses from being used.  A driver that DERIVES the
 * address instead has no such check -- it would happily write 0x2902801a8 on a
 * T600x (M1 Pro/Max), where the ANS apertures are 0x38F400000/0x393CC0000/
 * 0x393C50000 and 0x290280000 is not the pmgr_east block at all.
 *
 * So the derivation is gated on the ANS coprocessor aperture firmware actually
 * published.  It is SoC-specific and it is data the driver already has:
 *   T602x: cpu 0x347400000, mailbox 0x347408000, nvme 0x34BCC0000,
 *          sart 0x34BC50000  (Asahi t602x-nvme.dtsi; byte-exact match with the
 *          live-ADT values Mu printed on this J414s, WIP.md 2026-07-28)
 * Any other base refuses the derivation and leaves the reset unavailable,
 * which fails loudly in telemetry rather than writing an unknown register.
 */
#define NTASI_ANS_T602X_ASC_BASE            UINT64_C(0x347400000)

#define NTASI_ANS_PMGR_T602X_EAST_BASE      UINT64_C(0x290280000)
#define NTASI_ANS_PMGR_OFFSET_APCIE_ST      UINT64_C(0x1a0)
#define NTASI_ANS_PMGR_OFFSET_ANS2          UINT64_C(0x1a8)
#define NTASI_ANS_PMGR_OFFSET_APCIE_ST_SYS  UINT64_C(0x408)
#define NTASI_ANS_PMGR_OFFSET_APCIE_ST1_SYS UINT64_C(0x410)
#define NTASI_ANS_PMGR_WORD_SIZE            UINT64_C(4)

/*
 * ------------------------------------------------------------------
 * T8142 (M5, J813) -- A SECOND SoC, AND WHY THE LAYOUT HAD TO BECOME DATA.
 * ------------------------------------------------------------------
 *
 * Everything above is T602x.  It was written as compile-time constants with
 * ntasi_ans_pmgr_validate_layout() hard-comparing against
 * NTASI_ANS_PMGR_T602X_EAST_BASE, which made "wrong SoC" and "wrong address"
 * the same rejection.  On T8142 that is a false negative: the addresses are
 * right, they simply belong to a different die.  MEASURED 2026-08-12 from
 * Mu's own live-ADT walk on this J813, printed on the UART before Windows
 * starts:
 *
 *   ASC/cpu aperture 0x481600000  (vs T602x 0x347400000)
 *   nvme 0x485CC0000, sart 0x485C50000
 *   PMGR "ANS"          @0x380700300 = 0x0F0020FF  actual=0xF target=0xF
 *   PMGR "APCIE_ST"     @0x380700410 = 0x000002FF  actual=0xF target=0xF
 *   PMGR "APCIE_SYS_ST" @0x380700520 = 0x1000030F  actual=0x0 target=0xF
 *   (no APCIE_ST1_SYS on this SoC)
 *
 * All three live in the SAME 4 KiB page, 0x380700000, which is `pmgr` reg
 * tuple 0 -- not a separate pmgr_east die block as on T602x.  T8142 also
 * dropped the ADT `ps-regs` table for `ps-groups`, which is why Mu resolves
 * only three of the four ANS domain names here (the "ANS2"/"APCIE_ST_SYS"
 * spellings do not exist; "ANS"/"APCIE_SYS_ST" do).
 *
 * APCIE_SYS_ST IS DELIBERATELY NOT REQUIRED -- and the reason first written
 * here was wrong, so it is corrected rather than propagated.  It said "a
 * power-on write would set the target it already has and then poll to a
 * timeout".  It would not: ntasi_ans_pmgr_power_on_value() also CLEARS
 * AUTO_ENABLE, exactly as m1n1's pmgr_set_mode() and Linux's
 * apple_pmgr_ps_set() do, and bit 28 is the bit that is set in 0x1000030F.
 * The write is not a no-op; it would take a domain the hardware is managing
 * automatically and pin it under software control for no reason.
 *
 * The correct reason to leave it alone: 0x1000030F is AUTO_ENABLE=1 with
 * PS_TARGET=0xF, which Linux's apple_pmgr_ps_is_active() counts as ACTIVE.
 * The domain is alive, under hardware auto power management, and wants no
 * bring-up at all.  It is not a missing bring-up step: Mu's
 * own AppleANS DXE completes every stage and reports "namespace 1 ready:
 * 122138133 blocks x 4096 bytes" with this domain in exactly that state, on
 * the same boot the values above were captured.  So it is ADVISORY -- read,
 * recorded and printed, never written, never fatal.
 */
#define NTASI_ANS_T8142_ASC_BASE                  UINT64_C(0x481600000)
#define NTASI_ANS_PMGR_T8142_BASE                 UINT64_C(0x380700000)
#define NTASI_ANS_PMGR_T8142_OFFSET_ANS2          UINT64_C(0x300)
#define NTASI_ANS_PMGR_T8142_OFFSET_APCIE_ST      UINT64_C(0x410)
#define NTASI_ANS_PMGR_T8142_OFFSET_APCIE_ST_SYS  UINT64_C(0x520)

/*
 * What this driver is allowed to do with a domain, per SoC.
 *
 * ABSENT   -- the SoC has no such domain.  Its address must be 0; nothing
 *             reads it, nothing writes it, and validate_layout treats a
 *             non-zero address for it as a layout error rather than
 *             silently trusting a word nobody can identify.
 * ADVISORY -- the word exists and is read into the preflight record, but the
 *             bring-up sequence never writes it and never fails on it.
 * REQUIRED -- part of the dependency chain; powered on parent-first, and a
 *             failure stops the sequence before any later domain is touched.
 */
enum ntasi_ans_pmgr_domain_policy {
    NTASI_ANS_PMGR_POLICY_ABSENT = 0,
    NTASI_ANS_PMGR_POLICY_ADVISORY = 1,
    NTASI_ANS_PMGR_POLICY_REQUIRED = 2,
};

/*
 * A SoC's ANS PMGR geometry, selected by the one piece of SoC identity the
 * driver always has before it touches anything: the ASC aperture firmware
 * published in _CRS.  `offset` and `policy` are indexed by
 * enum ntasi_ans_pmgr_domain_id, NOT by sequence position.
 */
struct ntasi_ans_pmgr_soc_layout {
    const char *name;
    uint64_t asc_base;
    uint64_t block_base;
    uint64_t offset[NTASI_ANS_PMGR_DOMAIN_COUNT];
    uint8_t policy[NTASI_ANS_PMGR_DOMAIN_COUNT];
};

/*
 * Resolve the SoC layout from the ASC aperture, or NULL for an ASC base this
 * driver has never measured.  NULL is the honest answer and the caller must
 * treat it as "no PMGR word may be read or written here" -- deriving an
 * address from another SoC's constants is precisely the failure this gate
 * exists to prevent.
 */
const struct ntasi_ans_pmgr_soc_layout *
ntasi_ans_pmgr_soc_for_asc_base(uint64_t asc_base);

struct ntasi_ans_pmgr_domain_step {
    enum ntasi_ans_pmgr_domain_id domain;
    /* ACPI _CRS ACCESS_RANGE index, per Mu AcpiPlatform.c's documented ABI. */
    unsigned int resource_index;
    /* Expected offset inside the PMGR block (see the constants above). */
    uint64_t block_offset;
    /* ADT spelling, for diagnostics only -- never matched against data. */
    const char *adt_name;
};

/*
 * The ordered bring-up sequence.  Index i is performed before index i+1.
 * ntasi_ans_pmgr_power_on_sequence() is the only executor, and
 * AppleNvme.c drives both resource mapping and bring-up from this table so
 * the two can never disagree about which resource is which domain.
 */
extern const struct ntasi_ans_pmgr_domain_step
    ntasi_ans_pmgr_power_sequence[NTASI_ANS_PMGR_DOMAIN_COUNT];

enum ntasi_ans_pmgr_layout_status {
    NTASI_ANS_PMGR_LAYOUT_OK = 0,
    NTASI_ANS_PMGR_LAYOUT_ERR_ARGUMENT = -40,
    NTASI_ANS_PMGR_LAYOUT_ERR_ALIGNMENT = -41,
    NTASI_ANS_PMGR_LAYOUT_ERR_OFFSET = -42,
    NTASI_ANS_PMGR_LAYOUT_ERR_BLOCK = -43,
    NTASI_ANS_PMGR_LAYOUT_ERR_DUPLICATE = -44,
};

/*
 * Validate the four physical addresses firmware handed us before any write.
 *
 * This exists because a wrong PMGR base is not a benign failure: at the time
 * of writing, Mu's PcdAppleAnsPmgr*Base values use the 0x28e080000 `pmgr`
 * base rather than the 0x290280000 `pmgr_east` base, which makes
 * base+0x1a0/0x1a8 land on ps_dcs_09/ps_dcs_10 -- always-on DRAM controller
 * power-state registers.  Writing a power-state target there would power-
 * manage memory controllers.  So the driver refuses to touch any PMGR word
 * whose address it cannot positively identify as an ANS domain.
 *
 * `addresses` is indexed by enum ntasi_ans_pmgr_domain_id.
 * On failure, *failed_step (if non-NULL) receives the offending index.
 *
 * `soc` names which SoC's geometry the addresses are being judged against and
 * must not be NULL -- see ntasi_ans_pmgr_soc_for_asc_base().  A domain the
 * layout marks ABSENT must have address 0 and is otherwise skipped; every
 * other domain must be exactly soc->block_base + soc->offset[domain].
 */
int ntasi_ans_pmgr_validate_layout(const uint64_t *addresses,
                                   unsigned int count,
                                   const struct ntasi_ans_pmgr_soc_layout *soc,
                                   unsigned int *failed_step);

struct ntasi_ans_pmgr_sequence_ops {
    /* Per-domain opaque for reset_ops read32/write32; NULL fails closed. */
    void *(*domain)(void *context, enum ntasi_ans_pmgr_domain_id domain);
    /* Optional telemetry hook invoked before the domain is touched. */
    void (*begin)(void *context, const struct ntasi_ans_pmgr_domain_step *step);
    /* Optional per-domain result hook, invoked with the power status. */
    void (*done)(void *context, const struct ntasi_ans_pmgr_domain_step *step,
                 int status);
};

/*
 * Power on every REQUIRED domain in ntasi_ans_pmgr_power_sequence order,
 * stopping at the first failure and never touching a later domain.  Returns
 * NTASI_ANS_PMGR_POWER_OK (or the argument error), and on failure returns
 * the failing domain's status with *failed_step set to its table index.
 *
 * Domains the SoC layout marks ABSENT or ADVISORY are stepped over: their
 * `done` hook still fires, with NTASI_ANS_PMGR_POWER_OK_SKIPPED, so the log
 * records the decision rather than leaving a silent gap in the sequence.
 * `soc` must not be NULL.
 */
int ntasi_ans_pmgr_power_on_sequence(
    const struct ntasi_ans_pmgr_reset_ops *ops,
    const struct ntasi_ans_pmgr_sequence_ops *sequence_ops,
    const struct ntasi_ans_pmgr_soc_layout *soc,
    void *context, uint32_t poll_limit, unsigned int *failed_step);

uint32_t ntasi_ans_pmgr_disable_value(uint32_t current);
uint32_t ntasi_ans_pmgr_assert_value(uint32_t current);
uint32_t ntasi_ans_pmgr_deassert_value(uint32_t current);
uint32_t ntasi_ans_pmgr_enable_value(uint32_t current);
int ntasi_ans_pmgr_reset_assert(const struct ntasi_ans_pmgr_reset_ops *ops,
                                void *opaque, uint32_t poll_limit);
int ntasi_ans_pmgr_reset_deassert(const struct ntasi_ans_pmgr_reset_ops *ops,
                                  void *opaque, uint32_t poll_limit);
uint32_t ntasi_ans_pmgr_power_on_value(uint32_t current);
uint32_t ntasi_ans_pmgr_auto_enable_value(uint32_t programmed);
int ntasi_ans_pmgr_power_on(const struct ntasi_ans_pmgr_reset_ops *ops,
                            void *opaque, uint32_t poll_limit);

/* IO command opcodes (nvme.c:64-66). */
#define NTASI_ANS_CMD_FLUSH 0x00u
#define NTASI_ANS_CMD_WRITE 0x01u
#define NTASI_ANS_CMD_READ  0x02u

/* NVMMU TCB DMA direction flags (Linux apple.c:106-107, 833-837). */
#define NTASI_ANS_TCB_DMA_FROM_DEVICE (1u << 0)
#define NTASI_ANS_TCB_DMA_TO_DEVICE   (1u << 1)

enum ntasi_ans_dma_direction {
    NTASI_ANS_DMA_FROM_DEVICE = NTASI_ANS_TCB_DMA_FROM_DEVICE,
    NTASI_ANS_DMA_TO_DEVICE = NTASI_ANS_TCB_DMA_TO_DEVICE,
};

enum ntasi_ans_submission_mode {
    NTASI_ANS_SUBMISSION_CONVENTIONAL = 0,
    NTASI_ANS_SUBMISSION_LINEAR_NVMMU = 1,
};

struct ntasi_ans_hw {
    enum ntasi_ans_submission_mode submission_mode;
    uint32_t max_queue_depth;
    uint32_t admin_queue_depth;
    uint32_t io_command_stride;
    /*
     * WHICH ANS2-ERA REGISTERS THIS GENERATION STILL HAS.
     *
     * These are not tuning knobs -- touching an absent one is a fabric error,
     * not a no-op.  T8142 deleted three of them, and reading them is what
     * produced "ESR 0x92000410 EC=0x24 DABORT_LOWER, FAR 0x485cc1210" plus an
     * SError on J813 2026-08-14.  m1n1 src/nvme.c:714-722 records the same
     * for +0x24908 ("raises an asynchronous external abort ... L2C error
     * address ...e4908").  Mirrors the flags Mu's AppleNANDStorageDxe copy of
     * this core already carries.
     */
    /* +0x24908 LINEAR_SQ_CTRL.  Absent on T8142; reads AND writes abort. */
    bool linear_sq_ctrl_present;
    /* +0x24008 PRP-null-check control.  Absent on T8142. */
    bool prp_null_check_ctrl_present;
    /* +0x1210 as the legacy MAX_PEND_CMDS pending-count control. */
    bool max_pend_cmds_ctrl_present;
    /*
     * T8142 instead admits its linear I/O queues through the secure NVMe BAR
     * at +0x1200..+0x1210 (IOQA +0x1210, IOCQ +0x1208/+0x120c, IOSQ
     * +0x1200/+0x1204, in that order, each followed by dsb sy).
     */
    bool secure_io_queue_registers;
    /* T8152: publish the low word only after the complete high address. */
    bool queue_address_high_first;
};

/* T8142 secure I/O queue registration window (SPTM order, m1n1
 * nvme_t8142_register_io_queues()).  IOQA shares +0x1210 with the register
 * the older generations called MAX_PEND_CMDS; they are not the same thing and
 * they are never both present. */
#define NTASI_ANS_REG_T8142_IOSQ_ADDR 0x1200u
#define NTASI_ANS_REG_T8142_IOCQ_ADDR 0x1208u
#define NTASI_ANS_REG_T8142_IOQA      0x1210u

extern const struct ntasi_ans_hw ntasi_ans_hw_t8015;
extern const struct ntasi_ans_hw ntasi_ans_hw_t8152;
extern const struct ntasi_ans_hw ntasi_ans_hw_t8103;
extern const struct ntasi_ans_hw ntasi_ans_hw_t8142;

/* ------------------------------------------------------------------ */
/* Queue entry layouts (nvme.c:68-106, 119-121)                        */
/* ------------------------------------------------------------------ */

/*
 * PACKING PORTABILITY -- read before moving any struct across this line.
 *
 * ntasi_ans_sqe/ntasi_ans_cqe/ntasi_ans_tcb below are ANS2 wire-ABI layouts
 * (spec-adjacent NVMe SQE/CQE plus Apple's proprietary NVMMU TCB) and must be
 * byte-exact. GCC/Clang express that with `__attribute__((packed))`; MSVC's
 * cl.exe cannot parse that syntax at all, which matters because a Windows
 * ANS/NVMe kernel driver built with cl.exe would include this header (see
 * src/agx-initdata-core/agx_initdata.h for the precedent: this exact
 * incompatibility blocked all 14 `gpu/agxkmd` translation units, 1530 error
 * lines from one root cause, until that seam adopted the same fix applied
 * here).
 *
 * So: the attribute becomes NTASI_ANS_PACKED (empty under MSVC), and MSVC
 * instead gets a `#pragma pack(push, 1)` region covering the same structs.
 *
 * WHY A REGION AND NOT THE WHOLE FILE: this header also contains
 * `struct ntasi_ans_cq_state` (below, CQ head/phase consumer state), which is
 * deliberately NOT packed -- it is host-side bookkeeping, not a wire struct.
 * It sits well outside this region (after the register-value helpers,
 * command builders, etc.), so a whole-file pragma is not needed and is not
 * used; only ntasi_ans_sqe/ntasi_ans_cqe/ntasi_ans_tcb (verified below, by
 * `grep -n "^struct \|attribute__((packed))\|^};" apple_nvme.h`, to be the
 * only three PACKED structs in this file and to be textually contiguous, with
 * no unpacked struct interleaved between them) are inside the pushed region.
 *
 * This is verified, not asserted: every struct in the region carries
 * `_Static_assert(sizeof(...) == N)` and per-field `offsetof` checks below,
 * so if MSVC's packing ever disagreed with the GCC/Clang layout the build
 * would fail rather than emit a driver with silently wrong ANS2 wire structs.
 */
#if defined(_MSC_VER)
#  define NTASI_ANS_PACKED
#  pragma pack(push, 1)
#else
#  define NTASI_ANS_PACKED __attribute__((packed))
#endif

/*
 * Submission queue entry -- 64 bytes. Identical to a spec NVMe SQE except
 * the 16-bit Command Identifier (spec bytes 2-3) is split into a u8 tag
 * (byte 2) + u8 rsvd (byte 3): "normal NVMe has tag as u16" (nvme.c:72).
 */
struct ntasi_ans_sqe {
    uint8_t opcode;   /* 0x00 */
    uint8_t flags;    /* 0x01 */
    uint8_t tag;      /* 0x02 -- u8, not u16 (nvme.c:71-72) */
    uint8_t rsvd;     /* 0x03 */
    uint32_t nsid;    /* 0x04 */
    uint32_t cdw2;    /* 0x08 */
    uint32_t cdw3;    /* 0x0c */
    uint64_t metadata; /* 0x10 */
    uint64_t prp1;    /* 0x18 */
    uint64_t prp2;    /* 0x20 */
    uint32_t cdw10;   /* 0x28 */
    uint32_t cdw11;   /* 0x2c */
    uint32_t cdw12;   /* 0x30 */
    uint32_t cdw13;   /* 0x34 */
    uint32_t cdw14;   /* 0x38 */
    uint32_t cdw15;   /* 0x3c */
} NTASI_ANS_PACKED;

_Static_assert(offsetof(struct ntasi_ans_sqe, tag) == 0x02,
               "sqe tag offset (u8, not spec u16 CID), nvme.c:71");
_Static_assert(offsetof(struct ntasi_ans_sqe, nsid) == 0x04, "sqe nsid offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, metadata) == 0x10, "sqe metadata offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, prp1) == 0x18, "sqe prp1 offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, prp2) == 0x20, "sqe prp2 offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, cdw10) == 0x28, "sqe cdw10 offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, cdw11) == 0x2c, "sqe cdw11 offset");
_Static_assert(offsetof(struct ntasi_ans_sqe, cdw12) == 0x30, "sqe cdw12 offset");
_Static_assert(sizeof(struct ntasi_ans_sqe) == 64, "sqe must be 64 bytes, nvme.c:119");

/*
 * Completion queue entry -- 16 bytes. Spec's u32 DW0 + u32 DW1 become one
 * u64 result; spec's sq_head/sq_id become a reserved u32 (nvme.c:87-92).
 */
struct ntasi_ans_cqe {
    uint64_t result; /* 0x00 -- spec: u32 DW0 + u32 DW1 */
    uint32_t rsvd;   /* 0x08 -- spec: sq_head (u16) + sq_id (u16) */
    uint16_t tag;    /* 0x0c -- spec: command identifier */
    uint16_t status; /* 0x0e -- bit0 phase, bits[15:1] status code */
} NTASI_ANS_PACKED;

_Static_assert(offsetof(struct ntasi_ans_cqe, rsvd) == 0x08, "cqe rsvd offset");
_Static_assert(offsetof(struct ntasi_ans_cqe, tag) == 0x0c, "cqe tag offset");
_Static_assert(offsetof(struct ntasi_ans_cqe, status) == 0x0e, "cqe status offset");
_Static_assert(sizeof(struct ntasi_ans_cqe) == 16, "cqe must be 16 bytes, nvme.c:120");

/*
 * NVMMU TCB (translation control block) -- 128 bytes, one per SQ slot,
 * indexed by tag. Authorizes DMA for that slot (nvme.c:94-106).
 */
struct ntasi_ans_tcb {
    uint8_t opcode;       /* 0x00 -- copy of SQE opcode */
    uint8_t dma_flags;    /* 0x01 -- FROM_DEVICE or TO_DEVICE */
    uint8_t command_id;   /* 0x02 -- == tag */
    uint8_t _unk0;        /* 0x03 -- 0 */
    uint16_t length;      /* 0x04 -- low 16 bits of SQE cdw12 */
    uint8_t _unk1[18];    /* 0x06 -- 0 */
    uint64_t prp1;        /* 0x18 -- copy of SQE prp1 */
    uint64_t prp2;        /* 0x20 -- copy of SQE prp2 */
    uint8_t _unk2[16];    /* 0x28 -- 0 */
    uint8_t aes_iv[8];    /* 0x38 -- 0, inline-crypto related, unused */
    uint8_t _aes_unk[64]; /* 0x40 -- 0 */
} NTASI_ANS_PACKED;

_Static_assert(offsetof(struct ntasi_ans_tcb, dma_flags) == 0x01, "tcb dma_flags offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, command_id) == 0x02, "tcb command_id offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, length) == 0x04, "tcb length offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, _unk1) == 0x06, "tcb unknown field offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, prp1) == 0x18, "tcb prp1 offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, prp2) == 0x20, "tcb prp2 offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, _unk2) == 0x28, "tcb unknown field offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, aes_iv) == 0x38, "tcb aes_iv offset");
_Static_assert(offsetof(struct ntasi_ans_tcb, _aes_unk) == 0x40, "tcb _aes_unk offset");
_Static_assert(sizeof(struct ntasi_ans_tcb) == 128, "tcb must be 128 bytes, nvme.c:121");

/* NTASI_ANS_PACK_POP -- end of the byte-exact ANS2 wire-ABI region opened
 * above. ntasi_ans_cq_state (below, host-side consumer bookkeeping) is NOT
 * packed and must stay outside the pragma region. */
#if defined(_MSC_VER)
#  pragma pack(pop)
#endif

/* ------------------------------------------------------------------ */
/* Register-value helpers (nvme.c:353-355, 366)                        */
/* ------------------------------------------------------------------ */

/* ((slots-1)<<16)|(slots-1); AQA, nvme.c:366. aqa(64) == 0x003F003F. */
uint32_t ntasi_ans_aqa(uint32_t slots);

/* Actual depth in each half; Linux apple.c programs (slots << 16) | slots. */
uint32_t ntasi_ans_max_pend_cmds(uint32_t slots);

/* slots-1; NVMMU_NUM, nvme.c:355. nvmmu_num(64) == 0x3F. */
uint32_t ntasi_ans_nvmmu_num(uint32_t slots);

/* NVM CSS, round-robin arbitration, 4 KiB MPS, 64 B SQE, and 16 B CQE. */
uint32_t ntasi_ans_cc_config(void);

/* ------------------------------------------------------------------ */
/* Doorbell offset selection (nvme.c:234-237, 270-273)                 */
/* ------------------------------------------------------------------ */

/* Linear-SQ tag doorbell: admin -> 0x2490c, IO -> 0x24910. */
uint32_t ntasi_ans_sq_db_off(bool admin);

/* Standard SQ tail doorbell for T8015's conventional queue mode. */
uint32_t ntasi_ans_conventional_sq_db_off(bool admin);

/* Spec-shaped CQ head doorbell: admin -> 0x1004, IO -> 0x100c. */
uint32_t ntasi_ans_cq_db_off(bool admin);

/* ------------------------------------------------------------------ */
/* Queue memory sizing (nvme.c:138-146)                                */
/* ------------------------------------------------------------------ */

size_t ntasi_ans_sq_bytes(uint32_t slots);  /* slots * 64  */
size_t ntasi_ans_cq_bytes(uint32_t slots);  /* slots * 16  */
size_t ntasi_ans_tcb_bytes(uint32_t slots); /* slots * 128 */
size_t ntasi_ans_command_bytes(const struct ntasi_ans_hw *hw,
                               bool admin, uint32_t slots);

/* ------------------------------------------------------------------ */
/* TCB packing from a command (nvme.c:222-228)                         */
/* ------------------------------------------------------------------ */

/*
 * Fill *tcb from *sqe as Linux's apple_nvme_submit_cmd() does: zero the TCB,
 * then opcode/command_id(=sqe->tag)/length(low 16 bits of sqe->cdw12)/PRPs,
 * and set the explicit DMA direction flag.
 * Caller must have set sqe->tag before calling (mirrors nvme.c setting
 * queue_cmd->tag = tag before the TCB is derived from queue_cmd).
 */
void ntasi_ans_tcb_fill(struct ntasi_ans_tcb *tcb,
                        const struct ntasi_ans_sqe *sqe,
                        enum ntasi_ans_dma_direction direction);

/* ------------------------------------------------------------------ */
/* Command builders (nvme.c:373-396, 485-494, 522-525, 541-549)        */
/* ------------------------------------------------------------------ */

/* NVME_CMD_FLUSH, nvme.c:522-525. Zeroes *c first. */
void ntasi_ans_cmd_flush(struct ntasi_ans_sqe *c, uint32_t nsid);

/*
 * Read/write command, modeled on nvme_read() (nvme.c:529-550): nsid, prp1,
 * cdw10 = lba & 0xffffffff, cdw11 = lba >> 32, cdw12 = block count, prp2 as
 * given (nvme.c only ever passes prp2 = 0; a second PRP page is unused for
 * the single 4 KiB block m1n1 issues). opcode is caller-supplied
 * (1 = NTASI_ANS_CMD_WRITE, 2 = NTASI_ANS_CMD_READ) so this builder covers
 * both; m1n1 itself only implements the READ path (there is no nvme_write()
 * in nvme.c) -- the WRITE field layout is a documented generalization by
 * symmetry with spec-standard NVMe RW command fields, not independently
 * verified in nvme.c. Zeroes *c first.
 */
void ntasi_ans_cmd_rw(struct ntasi_ans_sqe *c, uint8_t opcode, uint32_t nsid, uint64_t lba,
                      uint32_t cdw12, uint64_t prp1, uint64_t prp2);

/* CREATE I/O COMPLETION QUEUE, nvme.c:373-384. Zeroes *c first. */
void ntasi_ans_cmd_create_iocq(struct ntasi_ans_sqe *c, uint16_t qid, uint32_t slots,
                               uint64_t prp1);

/* CREATE I/O SUBMISSION QUEUE, nvme.c:386-396. Zeroes *c first. */
void ntasi_ans_cmd_create_iosq(struct ntasi_ans_sqe *c, uint16_t qid, uint16_t cqid,
                               uint32_t slots, uint64_t prp1);

/*
 * DELETE I/O SUBMISSION/COMPLETION QUEUE (nvme.c:482-494). opcode is
 * NTASI_ANS_ADMIN_CMD_DELETE_SQ (0x00) or _DELETE_CQ (0x04). Zeroes *c
 * first.
 */
void ntasi_ans_cmd_delete_q(struct ntasi_ans_sqe *c, uint8_t opcode, uint16_t qid);

/* ------------------------------------------------------------------ */
/* CQ head/phase consumer state machine (nvme.c:153-154, 242-268, 282)  */
/* ------------------------------------------------------------------ */

struct ntasi_ans_cq_state {
    uint8_t head;
    uint8_t phase;
};

/* Initial state after queue allocation: head 0, phase 1 (nvme.c:153-154). */
#define NTASI_ANS_CQ_STATE_INIT ((struct ntasi_ans_cq_state){.head = 0, .phase = 1})

/* A CQE is fresh (not yet consumed) when (status & 1) == phase, nvme.c:248. */
bool ntasi_ans_cqe_ready(uint16_t cqe_status, uint8_t phase);

/* Status code = status >> 1; 0 is success (nvme.c:282-284). */
uint16_t ntasi_ans_cqe_code(uint16_t cqe_status);

/*
 * Advance the consumer: head += 1; when head reaches `slots`, wrap to 0 and
 * flip phase (nvme.c:264-268).
 */
void ntasi_ans_cq_advance(struct ntasi_ans_cq_state *st, uint32_t slots);

/* ------------------------------------------------------------------ */
/* Free-space arithmetic for the bounded write test                     */
/* ------------------------------------------------------------------ */

/*
 * These live in the host-testable core precisely because of what depends on
 * them: they choose which block of a real, in-use disk a write test is allowed
 * to touch.  That decision must be verifiable on the host against a recorded
 * partition layout, not only observable after the fact on hardware.
 */
struct ntasi_ans_lba_extent {
    uint64_t start;
    uint64_t end; /* inclusive, as GPT records it */
};

/*
 * Largest run of blocks in [first_usable, last_usable] covered by no extent.
 * Returns false when there is no such run.  Extents may be unordered and may
 * overlap; nothing about their ordering is trusted.
 */
bool ntasi_ans_largest_free_run(const struct ntasi_ans_lba_extent *extents,
                                uint32_t count,
                                uint64_t first_usable,
                                uint64_t last_usable,
                                uint64_t *run_start,
                                uint64_t *run_end);

/* True when [lba, lba + blocks) touches any extent.  blocks must be >= 1. */
bool ntasi_ans_extent_intersects(const struct ntasi_ans_lba_extent *extents,
                                 uint32_t count,
                                 uint64_t lba,
                                 uint64_t blocks);

#endif /* NTASI_APPLE_NVME_H */
