; SPDX-License-Identifier: MIT
; air2ir3 synthetic fixture (hand-written) for the O2 LLVM-vs-NIR semantic
; oracle: strict (no fast-math flags) arithmetic with signed zero, infinity
; and NaN constants, fneg, a two-source shuffle, extract/insertelement, a
; constant-buffer array GEP and a struct return (every member written),
; written to three render targets (float4, float2, float). The struct base is
; undef, not zeroinitializer: the LLVM 18 interpreter used as the oracle
; aborts on aggregate zero constants ("Constant unimplemented for type").
source_filename = "o2_ops"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

define <{ <4 x float>, <2 x float>, float }> @o2_ops(<4 x float> %a, <4 x float> %b, ptr addrspace(2) %cb) {
  %k.p = getelementptr inbounds [4 x <4 x float>], ptr addrspace(2) %cb, i64 0, i64 2
  %k = load <4 x float>, ptr addrspace(2) %k.p, align 16
  %s = fsub <4 x float> %a, %b
  %n = fneg <4 x float> %s
  %c = fadd <4 x float> %n, <float -0.000000e+00, float 0x7FF0000000000000, float 0x7FF8000000000000, float 1.000000e+00>
  %m = fmul <4 x float> %c, %k
  %sh = shufflevector <4 x float> %m, <4 x float> %a, <2 x i32> <i32 5, i32 2>
  %e = extractelement <4 x float> %b, i64 3
  %x = fmul float %e, %e
  %y = fsub float %x, 0.000000e+00
  %sh2 = insertelement <2 x float> %sh, float %y, i64 1
  %r0 = insertvalue <{ <4 x float>, <2 x float>, float }> undef, <4 x float> %m, 0
  %r1 = insertvalue <{ <4 x float>, <2 x float>, float }> %r0, <2 x float> %sh2, 1
  %r2 = insertvalue <{ <4 x float>, <2 x float>, float }> %r1, float %x, 2
  ret <{ <4 x float>, <2 x float>, float }> %r2
}

!air.fragment = !{!0}
!0 = !{ptr @o2_ops, !1, !5}
!1 = !{!2, !3, !4}
!2 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"rt0"}
!3 = !{!"air.render_target", i32 1, i32 0, !"air.arg_type_name", !"float2", !"air.arg_name", !"rt1"}
!4 = !{!"air.render_target", i32 2, i32 0, !"air.arg_type_name", !"float", !"air.arg_name", !"rt2"}
!5 = !{!6, !7, !8}
!6 = !{i32 0, !"air.fragment_input", !"generated(1aDv4_f)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"a"}
!7 = !{i32 1, !"air.fragment_input", !"generated(1bDv4_f)", !"air.center", !"air.no_perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"b"}
!8 = !{i32 2, !"air.buffer", !"air.location_index", i32 3, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 64, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4x4", !"air.arg_name", !"cb"}
