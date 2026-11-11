; SPDX-License-Identifier: MIT
; air2ir3 synthetic fixture: returned struct built on zeroinitializer with
; member 1 never written, so render target 1 must receive (+0, +0). Checked on
; the NIR side only (the LLVM 18 interpreter cannot materialize aggregate zero
; constants); the expected value follows from LLVM IR semantics.
source_filename = "o2_zero_member"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

define <{ <4 x float>, <2 x float> }> @o2_zero_member(<4 x float> %a) {
  %r0 = insertvalue <{ <4 x float>, <2 x float> }> zeroinitializer, <4 x float> %a, 0
  ret <{ <4 x float>, <2 x float> }> %r0
}

!air.fragment = !{!0}
!0 = !{ptr @o2_zero_member, !1, !4}
!1 = !{!2, !3}
!2 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"rt0"}
!3 = !{!"air.render_target", i32 1, i32 0, !"air.arg_type_name", !"float2", !"air.arg_name", !"rt1"}
!4 = !{!5}
!5 = !{i32 0, !"air.fragment_input", !"generated(1aDv4_f)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"a"}
