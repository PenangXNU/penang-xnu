; SPDX-License-Identifier: MIT
; air2ir3 synthetic fixture (hand-written, not derived from any Apple binary).
; MVP-AIR-0 vertex entry: position = in.position * cb[1] + cb[0];
;                         color    = in.color.zyxw (swizzle) ; uv = in.uv
; Exercises: vertex_input, constant buffer (addrspace 2) GEP+load, fmul/fadd,
; shufflevector swizzle, insertvalue into the returned packed struct.
source_filename = "vs_scale"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-ios18.0.0"

%struct.xform = type { <4 x float>, <4 x float> }

define <{ <4 x float>, <4 x float>, <2 x float> }> @vs_scale(<4 x float> %pos, <4 x float> %color, <2 x float> %uv, ptr addrspace(2) %cb) {
  %bias.p = getelementptr inbounds %struct.xform, ptr addrspace(2) %cb, i64 0, i32 0
  %bias = load <4 x float>, ptr addrspace(2) %bias.p, align 16
  %scale.p = getelementptr inbounds %struct.xform, ptr addrspace(2) %cb, i64 0, i32 1
  %scale = load <4 x float>, ptr addrspace(2) %scale.p, align 16
  %m = fmul <4 x float> %pos, %scale
  %p = fadd <4 x float> %m, %bias
  %c = shufflevector <4 x float> %color, <4 x float> poison, <4 x i32> <i32 2, i32 1, i32 0, i32 3>
  %r0 = insertvalue <{ <4 x float>, <4 x float>, <2 x float> }> undef, <4 x float> %p, 0
  %r1 = insertvalue <{ <4 x float>, <4 x float>, <2 x float> }> %r0, <4 x float> %c, 1
  %r2 = insertvalue <{ <4 x float>, <4 x float>, <2 x float> }> %r1, <2 x float> %uv, 2
  ret <{ <4 x float>, <4 x float>, <2 x float> }> %r2
}

!air.vertex = !{!0}
!0 = !{ptr @vs_scale, !1, !5}
!1 = !{!2, !3, !4}
!2 = !{!"air.position", !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!3 = !{!"air.vertex_output", !"generated(5colorDv4_f)", !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!4 = !{!"air.vertex_output", !"generated(2uvDv2_f)", !"air.arg_type_name", !"float2", !"air.arg_name", !"uv"}
!5 = !{!6, !7, !8, !9}
!6 = !{i32 0, !"air.vertex_input", !"air.location_index", i32 0, i32 1, !"air.arg_type_name", !"float4", !"air.arg_name", !"position"}
!7 = !{i32 1, !"air.vertex_input", !"air.location_index", i32 1, i32 1, !"air.arg_type_name", !"float4", !"air.arg_name", !"color"}
!8 = !{i32 2, !"air.vertex_input", !"air.location_index", i32 2, i32 1, !"air.arg_type_name", !"float2", !"air.arg_name", !"uv"}
!9 = !{i32 3, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 32, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"xform", !"air.arg_name", !"cb"}
