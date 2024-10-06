define dso_local <vscale x 2 x i64> @test1(<vscale x 16 x i1> %pg) #0 {
entry:
  %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
  %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.sdiv.u.nxv2i64(<vscale x 2 x i1> %0, <vscale x 2 x i64> shufflevector (<vscale x 2 x i64> insertelement (<vscale x 2 x i64> poison, i64 5, i64 0), <vscale x 2 x i64> poison, <vscale x 2 x i32> zeroinitializer), <vscale x 2 x i64> shufflevector (<vscale x 2 x i64> insertelement (<vscale x 2 x i64> poison, i64 3, i64 0), <vscale x 2 x i64> poison, <vscale x 2 x i32> zeroinitializer))
  ret <vscale x 2 x i64> %1
}
declare <vscale x 2 x i64> @llvm.aarch64.sve.dup.x.nxv2i64(i64) #1
declare <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1>) #1
declare <vscale x 2 x i64> @llvm.aarch64.sve.mul.u.nxv2i64(<vscale x 2 x i1>, <vscale x 2 x i64>, <vscale x 2 x i64>) #1
declare <vscale x 2 x i64> @llvm.aarch64.sve.sdiv.u.nxv2i64(<vscale x 2 x i1>, <vscale x 2 x i64>, <vscale x 2 x i64>) #1
attributes #0 = { noinline nounwind vscale_range(1,16) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+sve" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
!llvm.module.flags = !{!0}
!llvm.ident = !{!1}
!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 20.0.0git (https://github.com/llvm/llvm-project.git 6a1bdd9a2e2a089c85d24dd5d934681fa22cf9ed)"}


; === test1

; Initial selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 18 nodes:
;   t0: ch,glue = EntryToken
;   t9: nxv2i64 = insert_vector_elt undef:nxv2i64, Constant:i64<5>, Constant:i64<0>
;   t12: nxv2i64 = insert_vector_elt undef:nxv2i64, Constant:i64<3>, Constant:i64<0>
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;       t10: nxv2i64 = splat_vector Constant:i64<5>
;       t13: nxv2i64 = splat_vector Constant:i64<3>
;     t14: nxv2i64 = llvm.aarch64.sve.mul.u TargetConstant:i64<1422>, t4, t10, t13
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t14
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1

;start
; FIRST
; FOURTH
; FIRST
; FOURTH
; t18: i64 = Constant<15>
; t19: nxv2i64 = splat_vector Constant:i64<15>
; t20: nxv2i64 = undef
; t21: nxv2i64 = <<Unknown Node #709>> t4, t19, undef:nxv2i64

; Optimized lowered selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 11 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;     t20: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t4, Constant:i64<15>, undef:nxv2i64
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t20
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1

;other
; Optimized lowered selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 11 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;     t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t4, Constant:i64<15>, undef:nxv2i64
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1

;end

; Optimized lowered selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 13 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;       t10: nxv2i64 = splat_vector Constant:i64<5>
;       t13: nxv2i64 = splat_vector Constant:i64<3>
;     t18: nxv2i64 = AArch64ISD::MUL_PRED t4, t10, t13
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1



; Type-legalized selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 13 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;       t10: nxv2i64 = splat_vector Constant:i64<5>
;       t13: nxv2i64 = splat_vector Constant:i64<3>
;     t18: nxv2i64 = AArch64ISD::MUL_PRED t4, t10, t13
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1



; Legalized selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 12 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t19: nxv2i1 = AArch64ISD::REINTERPRET_CAST t2
;       t10: nxv2i64 = splat_vector Constant:i64<5>
;       t13: nxv2i64 = splat_vector Constant:i64<3>
;     t18: nxv2i64 = AArch64ISD::MUL_PRED t19, t10, t13
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1



; Optimized legalized selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 12 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t19: nxv2i1 = AArch64ISD::REINTERPRET_CAST t2
;       t10: nxv2i64 = splat_vector Constant:i64<5>
;       t13: nxv2i64 = splat_vector Constant:i64<3>
;     t18: nxv2i64 = AArch64ISD::MUL_PRED t19, t10, t13
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18
;   t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1


; ===== Instruction selection begins: %bb.0 'entry'

; ISEL: Starting selection on root node: t17: ch = AArch64ISD::RET_GLUE t16, Register:nxv2i64 $z0, t16:1
; ISEL: Starting pattern match
;   Morphed node: t17: ch = RET_ReallyLR Register:nxv2i64 $z0, t16, t16:1
; ISEL: Match complete!

; ISEL: Starting selection on root node: t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18

; ISEL: Starting selection on root node: t18: nxv2i64 = AArch64ISD::MUL_PRED t19, t10, t13
; ISEL: Starting pattern match
;   Initial Opcode index to 444723
;   Match failed at index 444727
;   Continuing at 444840
;   Skipped scope entry (due to false predicate) at index 444852, continuing at 444910
;   Morphed node: t18: nxv2i64 = MUL_ZI_D t10, TargetConstant:i32<3>
; ISEL: Match complete!

; ISEL: Starting selection on root node: t10: nxv2i64 = splat_vector Constant:i64<5>
; ISEL: Starting pattern match
;   Initial Opcode index to 445058
;   Match failed at index 445062
;   Continuing at 445588
;   Skipped scope entry (due to false predicate) at index 445592, continuing at 445640
;   Morphed node: t10: nxv2i64 = DUP_ZI_D TargetConstant:i32<5>, TargetConstant:i32<0>
; ISEL: Match complete!

; ISEL: Starting selection on root node: t15: nxv2i64 = Register $z0

; ISEL: Starting selection on root node: t0: ch,glue = EntryToken

; ===== Instruction selection ends:

; Selected selection DAG: %bb.0 'test1:entry'
; SelectionDAG has 9 nodes:
;     t0: ch,glue = EntryToken
;       t10: nxv2i64 = DUP_ZI_D TargetConstant:i32<5>, TargetConstant:i32<0>
;     t18: nxv2i64 = MUL_ZI_D t10, TargetConstant:i32<3>
;   t16: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t18
;   t17: ch = RET_ReallyLR Register:nxv2i64 $z0, t16, t16:1


; Total amount of phi nodes to update: 0
; *** MachineFunction at end of ISel ***