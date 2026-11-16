/* SPDX-License-Identifier: Apache-2.0 */
#include "p1_memory_admission.h"

#if defined(ARM64_BOARD_CONFIG_PENANG_PHYSICAL) || defined(PENANG_P2_PROTECTED)
#error P1 memory admission is not selected by the protected board
#endif
#if defined(PENANG_PHYSICAL) && !defined(PENANG_P1_DIAGNOSTIC)
#error P1 memory admission must not enter the protected P2 profile
#endif
#if defined(PENANG_P1_DIAGNOSTIC) && !defined(PENANG_P1_SANDBOX_COMPILE)
#error P1 integration remains a sandbox proposal pending separate review
#endif

#define PAGE UINT64_C(16384)
#define LIMIT UINT64_C(0x1000000000000)
struct range { uint64_t lo, hi; };
struct node {
    unsigned kind, disabled, status_seen, memory_type, request, cells;
    const uint8_t *reg;
    uint32_t reg_size;
};
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }
static int equal(const uint8_t *p, size_t n, const char *s)
{
    size_t i = 0;
    while (i < n && s[i] && p[i] == (uint8_t)s[i]) ++i;
    return i < n && p[i] == 0 && s[i] == 0;
}
static int memory_name(const uint8_t *p, size_t n)
{
    return n >= 7 && p[0]=='m' && p[1]=='e' && p[2]=='m' && p[3]=='o' &&
           p[4]=='r' && p[5]=='y' && (p[6] == '@' || p[6] == 0);
}
static int overlaps(uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{ return a < d && c < b; }
static int add_range(struct range *list, unsigned *count, unsigned limit, uint64_t base, uint64_t size)
{
    if (!size || base >= LIMIT || size > LIMIT - base || *count >= limit) return -1;
    list[(*count)++] = (struct range){base, base + size};
    return 0;
}

int p1_memory_admit(const struct p1_runtime_fdt *input, uint64_t base,
                    uint64_t size, uint64_t top)
{
    if (!size || base >= LIMIT || size > LIMIT-base || ((base|size|top)&(PAGE-1)) ||
        top <= base || top >= base+size) return P1_MEMORY_BAD_WINDOW;
    if (!input || !input->bytes || input->length < 40 || input->length > 2*1024*1024)
        return P1_MEMORY_BAD_FDT;
    const uint8_t *f = input->bytes;
    uint32_t total=be32(f+4), structure=be32(f+8), strings=be32(f+12), rsv=be32(f+16);
    uint32_t ss=be32(f+36), ns=be32(f+32);
    if (be32(f)!=0xd00dfeed || be32(f+20)!=17 || be32(f+24)>17 || total<40 ||
        total>input->length || structure<40 || strings<40 || rsv<40 || (rsv&7) ||
        (structure&3) || structure>total || ss>total-structure || strings>total ||
        ns>total-strings || overlaps(structure,structure+ss,strings,strings+ns)) return P1_MEMORY_BAD_FDT;
    if (input->physical_address < base || input->physical_address > top ||
        total > top-input->physical_address) return P1_MEMORY_FDT_NOT_RETAINED;
    struct range ram[16], reserved[128];
    unsigned nr=0, nx=0;
    uint32_t pos=rsv;
    for (;;) {
        if (pos>total || total-pos<16 || overlaps(pos,pos+16,structure,structure+ss) ||
            overlaps(pos,pos+16,strings,strings+ns)) return P1_MEMORY_BAD_FDT;
        uint64_t a=be64(f+pos), b=be64(f+pos+8); pos+=16;
        if (!a && !b) break;
        if (add_range(reserved,&nx,128,a,b)) return P1_MEMORY_BAD_FDT;
    }
    struct node stack[16];
    unsigned depth=0, nodes=0, root_cells=0, root_seen=0, reserve_seen=0;
    pos=structure;
    while (pos < structure+ss) {
        if (structure+ss-pos<4) return P1_MEMORY_BAD_FDT;
        uint32_t token=be32(f+pos); pos+=4;
        if (token==1) {
            if (depth==16 || ++nodes>4096) return P1_MEMORY_BAD_FDT;
            uint32_t start=pos;
            while (pos<structure+ss && f[pos]) ++pos;
            if (pos==structure+ss) return P1_MEMORY_BAD_FDT;
            unsigned kind=0;
            if (!depth) { if (root_seen++ || pos!=start) return P1_MEMORY_BAD_FDT; }
            else if (depth==1 && memory_name(f+start,pos-start+1)) kind=1;
            else if (depth==1 && equal(f+start,pos-start+1,"reserved-memory")) {
                if (reserve_seen++) return P1_MEMORY_BAD_FDT;
                kind=2;
            } else if (stack[depth-1].kind==2) kind=3;
            else if (stack[depth-1].kind==1 || stack[depth-1].kind==3) return P1_MEMORY_BAD_FDT;
            stack[depth++] = (struct node){.kind=kind};
            pos=(pos+4)&~3u;
        } else if (token==2) {
            if (!depth) return P1_MEMORY_BAD_FDT;
            struct node *n=&stack[--depth];
            if (!n->disabled && (n->kind==1 || n->kind==3)) {
                if (n->request && !n->reg) return P1_MEMORY_UNRESOLVED_RESERVATION;
                if (!n->reg || !n->reg_size || (n->reg_size%16) ||
                    (n->kind==1 && !n->memory_type)) return P1_MEMORY_BAD_FDT;
                for (uint32_t off=0; off<n->reg_size; off+=16) {
                    if (n->kind==1) {
                        if (add_range(ram,&nr,16,be64(n->reg+off),be64(n->reg+off+8))) return P1_MEMORY_BAD_FDT;
                    } else if (add_range(reserved,&nx,128,be64(n->reg+off),be64(n->reg+off+8))) return P1_MEMORY_BAD_FDT;
                }
            }
            if (n->kind==2 && n->cells!=7) return P1_MEMORY_BAD_FDT;
        } else if (token==3) {
            if (!depth || structure+ss-pos<8) return P1_MEMORY_BAD_FDT;
            uint32_t length=be32(f+pos), offset=be32(f+pos+4);pos+=8;
            if (length>structure+ss-pos || offset>=ns) return P1_MEMORY_BAD_FDT;
            const uint8_t *name=f+strings+offset, *value=f+pos;
            size_t room=ns-offset;
            unsigned terminated=0;
            for (size_t i=0;i<room;++i) if (!name[i]) { terminated=1;break; }
            if (!terminated) return P1_MEMORY_BAD_FDT;
            struct node *n=&stack[depth-1];
            if ((depth==1 || n->kind==2) && (equal(name,room,"#address-cells") || equal(name,room,"#size-cells"))) {
                unsigned bit=equal(name,room,"#address-cells")?1:2;
                unsigned *cells=depth==1?&root_cells:&n->cells;
                if ((*cells&bit) || length!=4 || be32(value)!=2) return P1_MEMORY_BAD_FDT;
                *cells|=bit;
            } else if (n->kind==2 && equal(name,room,"ranges")) {
                if (length || (n->cells&4)) return P1_MEMORY_BAD_FDT;
                n->cells|=4;
            } else if (equal(name,room,"status")) {
                if (n->status_seen++) return P1_MEMORY_BAD_FDT;
                if (length==9 && equal(value,length,"disabled")) n->disabled=1;
                else if (!(length==5 && equal(value,length,"okay")) &&
                         !(length==3 && equal(value,length,"ok"))) return P1_MEMORY_BAD_FDT;
            } else if ((n->kind==1 || n->kind==3) && equal(name,room,"reg")) {
                if (n->reg) return P1_MEMORY_BAD_FDT;
                n->reg=value;n->reg_size=length;
            } else if (n->kind==1 && equal(name,room,"device_type")) {
                if (n->memory_type || length!=7 || !equal(value,length,"memory")) return P1_MEMORY_BAD_FDT;
                n->memory_type=1;
            } else if (n->kind==3 && equal(name,room,"size")) {
                if (n->request || length!=8 || !be64(value)) return P1_MEMORY_BAD_FDT;
                n->request=1;
            }
            pos=(pos+length+3)&~3u;
        } else if (token==4) continue;
        else if (token==9) {
            if (depth || pos!=structure+ss || root_cells!=3 || !nr) return P1_MEMORY_BAD_FDT;
            unsigned contains=0;
            for (unsigned i=0;i<nr;++i) {
                if (ram[i].lo<=base && base+size<=ram[i].hi) ++contains;
                for (unsigned j=0;j<i;++j)
                    if (overlaps(ram[i].lo,ram[i].hi,ram[j].lo,ram[j].hi)) return P1_MEMORY_BAD_FDT;
            }
            if (contains!=1) return P1_MEMORY_BAD_WINDOW;
            for (unsigned i=0;i<nx;++i) {
                uint64_t lo=reserved[i].lo&~(PAGE-1), hi=(reserved[i].hi+PAGE-1)&~(PAGE-1);
                if (overlaps(base,base+size,lo,hi)) return P1_MEMORY_RESERVED_OVERLAP;
            }
            return P1_MEMORY_GEOMETRY_OK;
        } else return P1_MEMORY_BAD_FDT;
    }
    return P1_MEMORY_BAD_FDT;
}
