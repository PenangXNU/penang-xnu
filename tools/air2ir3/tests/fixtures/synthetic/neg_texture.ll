; SPDX-License-Identifier: MIT
; air2ir3 synthetic negative fixture: a texture argument binding is outside
; MVP-AIR-0 -> E_UNSUPPORTED_BINDING.
source_filename = "neg_texture"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

define <4 x float> @neg_texture(<4 x float> %color, ptr addrspace(1) %tex) {
  ret <4 x float> %color
}

!air.fragment = !{!0}
!0 = !{ptr @neg_texture, !1, !3}
!1 = !{!2}
!2 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"out"}
!3 = !{!4, !5}
!4 = !{i32 0, !"air.fragment_input", !"generated(5colorDv4_f)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!5 = !{i32 1, !"air.texture", !"air.location_index", i32 0, i32 1, !"air.sample", !"air.arg_type_name", !"texture2d<float, sample>", !"air.arg_name", !"tex"}
