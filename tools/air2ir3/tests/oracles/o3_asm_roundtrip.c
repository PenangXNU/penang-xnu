/* SPDX-License-Identifier: MIT */
/*
 * o3_asm_roundtrip.c - O3 oracle for an encoded ir3 binary (Adreno 619/a6xx):
 * decode it with Mesa's isaspec decoder, re-assemble that text with Mesa's ir3
 * assembler (an independent lexer/parser and encoder path) and require the
 * same machine words. A decoder/encoder disagreement, an undecodable word or
 * text the assembler rejects all fail.
 *
 *   a2i-o3-asm FILE.bin      exit 0 = identical, 1 = mismatch, 2 = setup error
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/glsl_types.h"
#include "freedreno/common/freedreno_dev_info.h"
#include "freedreno/isa/ir3-isa.h"
#include "ir3.h"
#include "ir3_assembler.h"
#include "ir3_compiler.h"
#include "ir3_shader.h"

#define A619_CHIP_ID 0x06010900ull

int
main(int argc, char **argv)
{
   if (argc != 2) {
      fprintf(stderr, "usage: %s FILE.bin\n", argv[0]);
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   if (!f) {
      fprintf(stderr, "cannot open %s: %s\n", argv[1], strerror(errno));
      return 2;
   }
   static uint8_t bin[1 << 20];
   size_t n = fread(bin, 1, sizeof(bin), f);
   int big = !feof(f);
   fclose(f);
   if (big || n == 0 || n % 8) {
      fprintf(stderr, "%s: size %zu is not a non-empty multiple of 8 below 1 MiB\n", argv[1], n);
      return 2;
   }

   char *text = NULL;
   size_t tlen = 0;
   FILE *t = open_memstream(&text, &tlen);
   if (!t)
      return 2;
   ir3_isa_disasm(bin, (int)n, t, &(struct isa_decode_options){.gpu_id = 600, .show_errors = true});
   fclose(t);
   if (strstr(text, "???") || strstr(text, "error")) {
      printf("FAIL decode: undecodable word(s)\n%s", text);
      free(text);
      return 1;
   }

   setenv("MESA_SHADER_CACHE_DISABLE", "true", 1); /* no ~/.cache side effects */
   glsl_type_singleton_init_or_ref();
   struct fd_dev_id id = {.gpu_id = 0, .chip_id = A619_CHIP_ID};
   const struct fd_dev_info *info = fd_dev_info_raw(&id);
   struct ir3_compiler *c =
      info ? ir3_compiler_create(NULL, &id, info, &(struct ir3_compiler_options){0}) : NULL;
   if (!c) {
      fprintf(stderr, "no ir3 compiler for chip 0x%llx\n", (unsigned long long)A619_CHIP_ID);
      return 2;
   }
   FILE *in = fmemopen(text, tlen, "r");
   struct ir3_kernel_info kinfo;
   memset(&kinfo, 0, sizeof(kinfo));
   struct ir3_shader *sh = in ? ir3_parse_asm(c, &kinfo, in) : NULL;
   if (in)
      fclose(in);
   int rc = 1;
   if (!sh || !sh->variants || !sh->variants->bin) {
      printf("FAIL assemble: Mesa ir3 assembler rejected the decoded text\n%s", text);
   } else {
      const struct ir3_shader_variant *v = sh->variants;
      size_t an = (size_t)v->info.sizedwords * 4;
      size_t k, words = n / 8;
      for (k = 0; k < words && 8 * k + 8 <= an; k++)
         if (memcmp(bin + 8 * k, (const uint8_t *)v->bin + 8 * k, 8))
            break;
      /* the assembler may pad to its own instruction-group size with nops */
      int pad_ok = 1;
      for (size_t p = n; p < an; p++)
         pad_ok &= ((const uint8_t *)v->bin)[p] == 0;
      if (k == words && an >= n && pad_ok) {
         printf("PASS words=%zu assembled_words=%zu\n", words, an / 8);
         rc = 0;
      } else {
         uint64_t a = 0, b = 0;
         if (k < words)
            memcpy(&a, bin + 8 * k, 8);
         if (8 * k + 8 <= an)
            memcpy(&b, (const uint8_t *)v->bin + 8 * k, 8);
         printf("FAIL word %zu: binary=%016llx reassembled=%016llx (sizes %zu/%zu)\n%s", k,
                (unsigned long long)a, (unsigned long long)b, n, an, text);
      }
   }
   if (sh)
      ir3_shader_destroy(sh);
   ir3_compiler_destroy(c);
   glsl_type_singleton_decref();
   free(text);
   return rc;
}
