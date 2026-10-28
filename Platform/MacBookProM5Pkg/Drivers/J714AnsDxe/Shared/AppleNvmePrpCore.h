/* SPDX-License-Identifier: MIT */
#ifndef NTASI_APPLE_NVME_PRP_CORE_H
#define NTASI_APPLE_NVME_PRP_CORE_H

#include <stddef.h>
#include <stdint.h>

#define NTASI_ANS_PRP_PAGE_SIZE 4096u
#define NTASI_ANS_PRPS_PER_PAGE (NTASI_ANS_PRP_PAGE_SIZE / sizeof(uint64_t))

enum ntasi_ans_prp_result {
    NTASI_ANS_PRP_OK = 0,
    NTASI_ANS_PRP_ERR_ARGUMENT = -200,
    NTASI_ANS_PRP_ERR_RANGE = -201,
    NTASI_ANS_PRP_ERR_LAYOUT = -202,
    NTASI_ANS_PRP_ERR_LIST_SPACE = -203,
};

struct ntasi_ans_dma_segment {
    uint64_t address;
    size_t length;
};

/* Each list page is CPU writable and has a controller-visible DMA address. */
struct ntasi_ans_prp_list_page {
    uint64_t *entries;
    uint64_t dma_address;
};

struct ntasi_ans_prp_mapping {
    uint64_t prp1;
    uint64_t prp2;
    size_t data_pages;
    size_t list_pages_used;
};

/* Number of PRP-list pages required after PRP1 for a transfer of this size. */
size_t ntasi_ans_prp_list_pages_required(uint64_t first_address,
                                         size_t transfer_size);

/*
 * Maps an ordered DMA scatter/gather list into NVMe PRP1/PRP2 and chained
 * list pages. Discontinuities are legal only on controller page boundaries.
 * Extra bytes at the end of the final DMA segment are ignored.
 */
int ntasi_ans_prp_build(const struct ntasi_ans_dma_segment *segments,
                        size_t segment_count,
                        size_t transfer_size,
                        struct ntasi_ans_prp_list_page *list_pages,
                        size_t list_page_count,
                        struct ntasi_ans_prp_mapping *mapping);

#endif
