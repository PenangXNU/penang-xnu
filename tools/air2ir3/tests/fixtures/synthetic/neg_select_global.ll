; SPDX-License-Identifier: MIT
; air2ir3 synthetic fixture: `select` whose first pointer operand is a global.
; Regression for the module reader (the result type of select is the type of
; that operand; globals used to be untyped -> "instruction result type
; unresolved"). dump-ir must succeed; check must stop at fcmp (outside MVP-AIR-0).
source_filename = "neg_select_global"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

@fallback = internal addrspace(2) global <4 x float> zeroinitializer, align 16

define <4 x float> @neg_select_global(<4 x float> %color, ptr addrspace(2) %cb) {
  %x = extractelement <4 x float> %color, i64 0
  %c = fcmp ogt float %x, 0.000000e+00
  %p = select i1 %c, ptr addrspace(2) @fallback, ptr addrspace(2) %cb
  %v = load <4 x float>, ptr addrspace(2) %p, align 16
  ret <4 x float> %v
}

!air.fragment = !{!0}
!0 = !{ptr @neg_select_global, !1, !3}
!1 = !{!2}
!2 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!3 = !{!4, !5}
!4 = !{i32 0, !"air.fragment_input", !"generated(5colorDv4_f)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!5 = !{i32 1, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"float4", !"air.arg_name", !"cb"}
