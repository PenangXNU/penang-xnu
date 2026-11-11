/* SPDX-License-Identifier: MIT */
#include "bridge_contract.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    FILE *file = fopen(argv[1], "rb");
    assert(file && fseek(file, 0, SEEK_END) == 0);
    long bytes = ftell(file);
    assert(bytes >= 40 && fseek(file, 0, SEEK_SET) == 0);
    void *fdt = malloc((size_t)bytes);
    assert(fdt && fread(fdt, 1, (size_t)bytes, file) == (size_t)bytes);
    fclose(file);
    struct m12_handoff_plan plan, repeat;
    int status = m12_bridge_plan_memory(fdt, (size_t)bytes, &plan);
    printf("%s\n", m12_status_name(status));
    if (!strcmp(argv[2], "reject")) {
        assert(status != M12_BRIDGE_OK && plan.dynamic_count == 0);
        free(fdt);
        return 0;
    }
    assert(status == M12_BRIDGE_OK && plan.dynamic_count == 8);
    assert(m12_bridge_plan_memory(fdt, (size_t)bytes, &repeat) == M12_BRIDGE_OK);
    assert(!memcmp(&plan, &repeat, sizeof(plan)));
    for (uint32_t i = 0; i < plan.dynamic_count; i++) {
        const struct m12_dynamic_placement *p = &plan.dynamic[i];
        uint64_t base = p->range.base, size = p->range.size;
        assert(!((base | size) & M12_PAGE_16K_MASK));
        assert(p->owner == M12_RESERVATION_FDT_REQUEST);
        printf("%s %llx %llx no-map=%u owner=fdt-request\n",
               (const char *)fdt + p->node_name_offset,
               (unsigned long long)base, (unsigned long long)size, p->no_map);
        uint32_t index = plan.reserved_count - plan.dynamic_count + i;
        for (uint32_t j = 0; j < index; j++) {
            uint64_t other = plan.reserved_ranges[j].base;
            assert(base + size <= other || other + plan.reserved_ranges[j].size <= base);
        }
    }
    free(fdt);
    return 0;
}
