/* SPDX-License-Identifier: MIT */
#include "AppleNvmePrpCore.h"

#include <stdbool.h>
#include <string.h>

struct dma_cursor {
    const struct ntasi_ans_dma_segment *segments;
    size_t count;
    size_t index;
    size_t offset;
};

static bool add_overflows_u64(uint64_t value, size_t add)
{
    return (uint64_t)add > UINT64_MAX - value;
}

static int cursor_take_contiguous(struct dma_cursor *cursor, size_t length,
                                  uint64_t *address)
{
    uint64_t expected;
    size_t remaining = length;

    while (cursor->index < cursor->count &&
           cursor->offset == cursor->segments[cursor->index].length) {
        cursor->index++;
        cursor->offset = 0;
    }
    if (cursor->index == cursor->count ||
        cursor->segments[cursor->index].length == 0 ||
        add_overflows_u64(cursor->segments[cursor->index].address,
                          cursor->offset))
        return NTASI_ANS_PRP_ERR_RANGE;
    *address = cursor->segments[cursor->index].address + cursor->offset;
    expected = *address;

    while (remaining != 0) {
        const struct ntasi_ans_dma_segment *segment;
        size_t available;
        size_t take;
        uint64_t current;

        if (cursor->index == cursor->count)
            return NTASI_ANS_PRP_ERR_RANGE;
        segment = &cursor->segments[cursor->index];
        if (segment->length == 0 || cursor->offset > segment->length ||
            add_overflows_u64(segment->address, cursor->offset))
            return NTASI_ANS_PRP_ERR_RANGE;
        current = segment->address + cursor->offset;
        if (current != expected)
            return NTASI_ANS_PRP_ERR_LAYOUT;
        available = segment->length - cursor->offset;
        take = available < remaining ? available : remaining;
        if (take == 0 || add_overflows_u64(current, take))
            return NTASI_ANS_PRP_ERR_RANGE;
        cursor->offset += take;
        remaining -= take;
        expected = current + take;
        if (cursor->offset == segment->length) {
            cursor->index++;
            cursor->offset = 0;
        }
    }
    return NTASI_ANS_PRP_OK;
}

static size_t pages_after_first(uint64_t first_address, size_t transfer_size)
{
    size_t first_capacity = NTASI_ANS_PRP_PAGE_SIZE -
        (size_t)(first_address & (NTASI_ANS_PRP_PAGE_SIZE - 1u));
    size_t remaining;

    if (transfer_size <= first_capacity)
        return 0;
    remaining = transfer_size - first_capacity;
    return (remaining - 1u) / NTASI_ANS_PRP_PAGE_SIZE + 1u;
}

size_t ntasi_ans_prp_list_pages_required(uint64_t first_address,
                                         size_t transfer_size)
{
    size_t data_pages = pages_after_first(first_address, transfer_size);
    if (transfer_size == 0 || data_pages <= 1)
        return 0;
    /* Every non-final list spends its last entry chaining to the next. */
    return (data_pages - 2u) / (NTASI_ANS_PRPS_PER_PAGE - 1u) + 1u;
}

int ntasi_ans_prp_build(const struct ntasi_ans_dma_segment *segments,
                        size_t segment_count,
                        size_t transfer_size,
                        struct ntasi_ans_prp_list_page *list_pages,
                        size_t list_page_count,
                        struct ntasi_ans_prp_mapping *mapping)
{
    struct dma_cursor cursor = {segments, segment_count, 0, 0};
    uint64_t first_address;
    uint64_t ignored_address;
    size_t first_length;
    size_t remaining;
    size_t data_pages;
    size_t required_lists;
    size_t list_index = 0;
    size_t entry_index = 0;
    size_t data_index;
    int status;

    if (segments == NULL || segment_count == 0 || transfer_size == 0 ||
        mapping == NULL)
        return NTASI_ANS_PRP_ERR_ARGUMENT;
    if (segments[0].address == 0 || segments[0].length == 0)
        return NTASI_ANS_PRP_ERR_RANGE;
    first_address = segments[0].address;
    first_length = NTASI_ANS_PRP_PAGE_SIZE -
        (size_t)(first_address & (NTASI_ANS_PRP_PAGE_SIZE - 1u));
    if (first_length > transfer_size)
        first_length = transfer_size;
    status = cursor_take_contiguous(&cursor, first_length, &ignored_address);
    if (status != NTASI_ANS_PRP_OK)
        return status;
    remaining = transfer_size - first_length;
    data_pages = pages_after_first(first_address, transfer_size);
    required_lists = ntasi_ans_prp_list_pages_required(first_address,
                                                        transfer_size);
    if (required_lists > list_page_count ||
        (required_lists != 0 && list_pages == NULL))
        return NTASI_ANS_PRP_ERR_LIST_SPACE;
    for (list_index = 0; list_index < required_lists; ++list_index) {
        if (list_pages[list_index].entries == NULL ||
            list_pages[list_index].dma_address == 0 ||
            (list_pages[list_index].dma_address &
             (NTASI_ANS_PRP_PAGE_SIZE - 1u)) != 0)
            return NTASI_ANS_PRP_ERR_LIST_SPACE;
        memset(list_pages[list_index].entries, 0, NTASI_ANS_PRP_PAGE_SIZE);
    }

    *mapping = (struct ntasi_ans_prp_mapping){
        .prp1 = first_address,
        .data_pages = data_pages + 1u,
        .list_pages_used = required_lists,
    };
    list_index = 0;
    entry_index = 0;
    for (data_index = 0; data_index < data_pages; ++data_index) {
        uint64_t page_address;
        size_t chunk = remaining < NTASI_ANS_PRP_PAGE_SIZE
                           ? remaining : NTASI_ANS_PRP_PAGE_SIZE;

        status = cursor_take_contiguous(&cursor, chunk, &page_address);
        if (status != NTASI_ANS_PRP_OK)
            return status;
        if ((page_address & (NTASI_ANS_PRP_PAGE_SIZE - 1u)) != 0)
            return NTASI_ANS_PRP_ERR_LAYOUT;
        remaining -= chunk;
        if (data_pages == 1) {
            mapping->prp2 = page_address;
            continue;
        }
        if (entry_index == NTASI_ANS_PRPS_PER_PAGE - 1u &&
            data_index + 1u < data_pages) {
            list_pages[list_index].entries[entry_index] =
                list_pages[list_index + 1u].dma_address;
            list_index++;
            entry_index = 0;
        }
        list_pages[list_index].entries[entry_index++] = page_address;
    }
    if (data_pages > 1)
        mapping->prp2 = list_pages[0].dma_address;
    return remaining == 0 ? NTASI_ANS_PRP_OK : NTASI_ANS_PRP_ERR_RANGE;
}
