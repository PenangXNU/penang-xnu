/* SPDX-License-Identifier: MIT */
/*
 * a2i_cli.c - air2ir3 host CLI (inspection and subset gate).
 *
 * Modes that need only the freestanding core + host_io:
 *   inspect FILE [--entry S]        container/entry summary as JSON
 *   dump-records FILE [--entry S]   generic bitstream block/record walk
 *   dump-ir FILE [--entry S]        decoded module IR (canonical text)
 *   check FILE [--entry S]          MVP-AIR-0 subset gate (exit 0/6/7/...)
 * The 'compile' mode lives in a2i_compile.c (links Mesa) and is declared weak
 * here so this translation unit also builds standalone for the O1 oracle.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_io.h"

int a2i_compile_main(int argc, char **argv) __attribute__((weak));
const char *m_name(const a2i_loaded *L);

static int
usage(void)
{
   fprintf(stderr,
           "usage: air2ir3 <inspect|dump-records|dump-ir|check|compile> FILE [--entry NAME|IDX]\n"
           "       compile FILE OUT_PREFIX [--entry NAME|IDX]\n");
   return A2I_E_USAGE;
}

static const char *
opt_entry(int argc, char **argv, int from)
{
   for (int i = from; i + 1 < argc; i++)
      if (!strcmp(argv[i], "--entry"))
         return argv[i + 1];
   return NULL;
}

/* MTLB TYPE tag. Only the values cross-checked against the module's
 * !air.vertex / !air.fragment / !air.kernel metadata are named; any other value
 * is reported numerically with type_name "unknown" instead of a guess. */
static const char *
mtlb_type_name(uint8_t t)
{
   static const char *const names[] = {"vertex", "fragment", "kernel"};
   return t < 3 ? names[t] : "unknown";
}

static int
do_inspect(const char *path, const char *entry)
{
   uint8_t *buf;
   uint64_t len;
   a2i_diag d;
   a2i_diag_clear(&d);
   if (a2i_read_file(path, &buf, &len, &d)) {
      a2i_print_diag(stderr, &d);
      return d.code;
   }
   a2i_arena ar;
   a2i_arena_init(&ar, a2i_host_allocator(), 512u << 20);
   a2i_mtlb m;
   if (a2i_mtlb_parse(buf, len, &ar, &m, &d)) {
      a2i_print_diag(stderr, &d);
      a2i_arena_release(&ar);
      free(buf);
      return d.code;
   }
   printf("{\n  \"file_version\": \"%u.%u\", \"u16_04\": %u, \"target_os\": \"0x%02x\", "
          "\"os\": \"%u.%u\",\n",
          m.file_ver_major, m.file_ver_minor, m.hdr_u16_04, m.target_os, m.os_major, m.os_minor);
   printf("  \"fat_slices\": %u, \"function_count\": %u,\n", m.fat_count, m.function_count);
   printf("  \"header_policy\": \"%s\",\n",
          a2i_mtlb_check_header_policy(&m, &(a2i_diag){0}) == A2I_OK ? "MVP_OK" : "REJECT");
   printf("  \"functions\": [\n");
   for (uint32_t i = 0; i < m.function_count; i++) {
      const a2i_mtlb_function *f = &m.functions[i];
      uint32_t lossy = 0;
      printf("    {\"index\": %u, \"name\": ", i);
      if (f->has_name && f->name)
         a2i_json_str(stdout, f->name, f->name_len, &lossy);
      else
         fputs("null", stdout);
      printf(", \"name_lossy\": %s", lossy ? "true" : "false");
      if (f->has_type)
         printf(", \"type\": %u, \"type_name\": \"%s\"", f->type, mtlb_type_name(f->type));
      else
         fputs(", \"type\": null, \"type_name\": null", stdout);
      if (f->has_vers)
         printf(", \"air\": \"%u.%u\", \"msl\": \"%u.%u\"", f->air_major, f->air_minor,
                f->lang_major, f->lang_minor);
      else
         fputs(", \"air\": null, \"msl\": null", stdout);
      if (f->has_mdsz)
         printf(", \"mdsz\": %llu", (unsigned long long)f->mdsz);
      else
         fputs(", \"mdsz\": null", stdout);
      if (f->has_hash) {
         fputs(", \"hash\": \"", stdout);
         a2i_hex(stdout, f->hash, sizeof(f->hash));
         fputc('"', stdout);
      } else
         fputs(", \"hash\": null", stdout);
      if (f->bitcode_in_bounds)
         printf(", \"bitcode_file_offset\": %llu", (unsigned long long)f->bitcode_file_offset);
      else
         fputs(", \"bitcode_file_offset\": null", stdout);
      printf(", \"unknown_tags\": %u, \"bitcode_in_bounds\": %s}%s\n", f->unknown_tags,
             f->bitcode_in_bounds ? "true" : "false", i + 1 < m.function_count ? "," : "");
   }
   printf("  ]\n}\n");
   if (entry) {
      uint32_t idx;
      if (a2i_mtlb_select(&m, entry, &idx, &d) || a2i_mtlb_check_entry(&m, idx, &d)) {
         a2i_print_diag(stderr, &d);
         a2i_arena_release(&ar);
         free(buf);
         return d.code;
      }
      fprintf(stderr, "entry %s -> index %u: integrity + version OK\n", entry, idx);
   }
   a2i_arena_release(&ar);
   free(buf);
   return 0;
}

static int
do_dump_records(const char *path, const char *entry)
{
   uint8_t *buf;
   uint64_t len;
   a2i_diag d;
   a2i_diag_clear(&d);
   if (a2i_read_file(path, &buf, &len, &d)) {
      a2i_print_diag(stderr, &d);
      return d.code;
   }
   a2i_arena ar;
   a2i_arena_init(&ar, a2i_host_allocator(), 512u << 20);
   a2i_mtlb m;
   uint32_t idx = 0;
   if (a2i_mtlb_parse(buf, len, &ar, &m, &d) ||
       a2i_mtlb_select(&m, entry ? entry : "0", &idx, &d) || a2i_mtlb_check_entry(&m, idx, &d)) {
      a2i_print_diag(stderr, &d);
      a2i_arena_release(&ar);
      free(buf);
      return d.code;
   }
   const a2i_mtlb_function *f = &m.functions[idx];
   int rc = a2i_dump_records(stdout, f->bitcode, f->mdsz, f->bitcode_file_offset, &d);
   if (rc)
      a2i_print_diag(stderr, &d);
   a2i_arena_release(&ar);
   free(buf);
   return rc ? d.code : 0;
}

static int
do_dump_ir(const char *path, const char *entry)
{
   a2i_loaded L;
   a2i_diag d;
   a2i_diag_clear(&d);
   a2i_status st = a2i_load_entry(path, entry, false, &L, &d);
   if (st) {
      a2i_print_diag(stderr, &d);
      a2i_loaded_free(&L);
      return st;
   }
   printf("entry %u %s\n", L.index, m_name(&L));
   a2i_dump_ir(stdout, &L.module);
   a2i_loaded_free(&L);
   return 0;
}

static int
do_check(const char *path, const char *entry)
{
   a2i_loaded L;
   a2i_diag d;
   a2i_diag_clear(&d);
   a2i_status st = a2i_load_entry(path, entry, true, &L, &d);
   if (st) {
      a2i_print_diag(stderr, &d);
      a2i_loaded_free(&L);
      return st;
   }
   a2i_layout lay;
   a2i_iface io;
   a2i_subset_report rep;
   const a2i_mtlb_function *fn = &L.mtlb.functions[L.index];
   if ((st = a2i_layout_parse(L.module.datalayout, &lay, &d)) ||
       (st = a2i_iface_extract(&L.module, &io, &d)) ||
       (st = a2i_subset_check(&L.module, &io, &lay, fn->name, fn->name_len, &rep, &d))) {
      a2i_print_diag(stderr, &d);
      a2i_loaded_free(&L);
      return st;
   }
   printf("OK %s stage=%s insts=%u in=%u out=%u\n", fn->name,
          io.stage == A2I_STAGE_VERTEX ? "vertex" : "fragment", rep.insts, io.nin, io.nout);
   a2i_loaded_free(&L);
   return 0;
}

const char *
m_name(const a2i_loaded *L)
{
   const a2i_mtlb_function *fn = &L->mtlb.functions[L->index];
   return fn->name ? fn->name : "";
}

int
main(int argc, char **argv)
{
   if (argc < 3)
      return usage();
   const char *mode = argv[1], *path = argv[2];
   if (!strcmp(mode, "inspect"))
      return do_inspect(path, opt_entry(argc, argv, 3));
   if (!strcmp(mode, "dump-records"))
      return do_dump_records(path, opt_entry(argc, argv, 3));
   if (!strcmp(mode, "dump-ir"))
      return do_dump_ir(path, opt_entry(argc, argv, 3));
   if (!strcmp(mode, "check"))
      return do_check(path, opt_entry(argc, argv, 3));
   if (!strcmp(mode, "compile")) {
      if (!a2i_compile_main)
         return fprintf(stderr, "this build has no Mesa backend (compile unavailable)\n"), A2I_E_USAGE;
      return a2i_compile_main(argc, argv);
   }
   return usage();
}
