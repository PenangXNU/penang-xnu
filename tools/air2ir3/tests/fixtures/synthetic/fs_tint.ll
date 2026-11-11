; SPDX-License-Identifier: MIT
; air2ir3 synthetic fixture (hand-written, not derived from any Apple binary).
; MVP-AIR-0 fragment entry with a non-struct return:
;   t = cb[0]; c = in.color * t;  c.w = in.uv.x - t.w;  out = -(-c)
; Exercises: fragment_input (perspective / no_perspective), constant buffer
; load without GEP, fmul, extractelement, fsub, insertelement, fneg.
source_filename = "fs_tint"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

define <4 x float> @fs_tint(<4 x float> %color, <2 x float> %uv, ptr addrspace(2) %cb) {
  %t = load <4 x float>, ptr addrspace(2) %cb, align 16
  %c = fmul <4 x float> %color, %t
  %u = extractelement <2 x float> %uv, i64 0
  %tw = extractelement <4 x float> %t, i64 3
  %w = fsub float %u, %tw
  %c2 = insertelement <4 x float> %c, float %w, i64 3
  %n = fneg <4 x float> %c2
  %o = fneg <4 x float> %n
  ret <4 x float> %o
}

!air.fragment = !{!0}
!0 = !{ptr @fs_tint, !1, !3}
!1 = !{!2}
!2 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!3 = !{!4, !5, !6}
!4 = !{i32 0, !"air.fragment_input", !"generated(5colorDv4_f)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!5 = !{i32 1, !"air.fragment_input", !"generated(2uvDv2_f)", !"air.center", !"air.no_perspective", !"air.arg_type_name", !"float2", !"air.arg_name", !"uv"}
!6 = !{i32 2, !"air.buffer", !"air.location_index", i32 1, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"tint"}
