; ModuleID = '<stdin>'
source_filename = "/Users/jorgebotto/code/llvm-project/clang/test/CodeGen/aarch64-sve-intrinsics/example1.c"
target datalayout = "e-m:e-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "aarch64"

; Function Attrs: noinline nounwind vscale_range(1,16)
define dso_local <vscale x 2 x i64> @test1(<vscale x 16 x i1> %pg, <vscale x 2 x i64> %op2) #0 {
entry:
  %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
  %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.mul.u.nxv2i64(<vscale x 2 x i1> %0, <vscale x 2 x i64> zeroinitializer, <vscale x 2 x i64> %op2)
  ret <vscale x 2 x i64> %1
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare <vscale x 2 x i64> @llvm.aarch64.sve.dup.x.nxv2i64(i64) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1>) #1

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare <vscale x 2 x i64> @llvm.aarch64.sve.mul.u.nxv2i64(<vscale x 2 x i1>, <vscale x 2 x i64>, <vscale x 2 x i64>) #1

; Function Attrs: noinline nounwind vscale_range(1,16)
define dso_local <vscale x 2 x i64> @test2(<vscale x 16 x i1> %pg, <vscale x 2 x i64> %op2) #0 {
entry:
  %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
  %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.sdiv.u.nxv2i64(<vscale x 2 x i1> %0, <vscale x 2 x i64> zeroinitializer, <vscale x 2 x i64> %op2)
  ret <vscale x 2 x i64> %1
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare <vscale x 2 x i64> @llvm.aarch64.sve.sdiv.u.nxv2i64(<vscale x 2 x i1>, <vscale x 2 x i64>, <vscale x 2 x i64>) #1

attributes #0 = { noinline nounwind vscale_range(1,16) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+sve" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }

!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 20.0.0git (https://github.com/llvm/llvm-project.git 6a1bdd9a2e2a089c85d24dd5d934681fa22cf9ed)"}