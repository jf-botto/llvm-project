define dso_local <vscale x 2 x i64> @test2(<vscale x 16 x i1> %pg) #0 {
entry:
  %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
  %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.dup.nxv2i64(<vscale x 2 x i64> undef, <vscale x 2 x i1> %0, i64 15)
  ret <vscale x 2 x i64> %1
}
declare <vscale x 2 x i64> @llvm.aarch64.sve.dup.x.nxv2i64(i64) #1
declare <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1>) #1
declare <vscale x 2 x i64> @llvm.aarch64.sve.mul.u.nxv2i64(<vscale x 2 x i1>, <vscale x 2 x i64>, <vscale x 2 x i64>) #1
attributes #0 = { noinline nounwind vscale_range(1,16) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+sve" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
!llvm.module.flags = !{!0}
!llvm.ident = !{!1}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{!"clang version 20.0.0git (https://github.com/llvm/llvm-project.git 6a1bdd9a2e2a089c85d24dd5d934681fa22cf9ed)"}



; === test2

; Initial selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 12 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;     t8: nxv2i64 = llvm.aarch64.sve.dup TargetConstant:i64<1184>, undef:nxv2i64, t4, Constant:i64<15>
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t8
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1



; Optimized lowered selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 11 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;     t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t4, Constant:i64<15>, undef:nxv2i64
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1



; Type-legalized selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 11 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t4: nxv2i1 = llvm.aarch64.sve.convert.from.svbool TargetConstant:i64<1182>, t2
;     t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t4, Constant:i64<15>, undef:nxv2i64
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1



; Legalized selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 10 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t13: nxv2i1 = AArch64ISD::REINTERPRET_CAST t2
;     t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t13, Constant:i64<15>, undef:nxv2i64
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1



; Optimized legalized selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 10 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t13: nxv2i1 = AArch64ISD::REINTERPRET_CAST t2
;     t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t13, Constant:i64<15>, undef:nxv2i64
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1


; ===== Instruction selection begins: %bb.0 'entry'

; ISEL: Starting selection on root node: t11: ch = AArch64ISD::RET_GLUE t10, Register:nxv2i64 $z0, t10:1
; ISEL: Starting pattern match
;   Morphed node: t11: ch = RET_ReallyLR Register:nxv2i64 $z0, t10, t10:1
; ISEL: Match complete!

; ISEL: Starting selection on root node: t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12

; ISEL: Starting selection on root node: t12: nxv2i64 = AArch64ISD::DUP_MERGE_PASSTHRU t13, Constant:i64<15>, undef:nxv2i64
; ISEL: Starting pattern match
;   Initial Opcode index to 440445
;   Match failed at index 440450
;   Continuing at 440879
;   Skipped scope entry (due to false predicate) at index 440883, continuing at 440926
;   Morphed node: t12: nxv2i64 = CPY_ZPmR_D undef:nxv2i64, t13, Constant:i64<15>
; ISEL: Match complete!

; ISEL: Starting selection on root node: t13: nxv2i1 = AArch64ISD::REINTERPRET_CAST t2
; ISEL: Starting pattern match
;   Initial Opcode index to 485281
;   TypeSwitch[nxv2i1] from 485287 to 485333
;   Morphed node: t13: nxv2i1 = COPY_TO_REGCLASS t2, TargetConstant:i32<5>
; ISEL: Match complete!

; ISEL: Starting selection on root node: t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0

; ISEL: Starting selection on root node: t9: nxv2i64 = Register $z0

; ISEL: Starting selection on root node: t7: i64 = Constant<15>
; ISEL: Starting pattern match
;   Initial Opcode index to 378353
;   TypeSwitch[i64] from 378354 to 378357
;   Created node: t18: i32 = MOVi32imm TargetConstant:i32<15>
;   Morphed node: t7: i64 = SUBREG_TO_REG TargetConstant:i64<0>, t18, TargetConstant:i32<16>
; ISEL: Match complete!

; ISEL: Starting selection on root node: t6: nxv2i64 = undef

; ISEL: Starting selection on root node: t1: nxv16i1 = Register %0

; ISEL: Starting selection on root node: t0: ch,glue = EntryToken

; ===== Instruction selection ends:

; Selected selection DAG: %bb.0 'test2:entry'
; SelectionDAG has 16 nodes:
;   t0: ch,glue = EntryToken
;         t2: nxv16i1,ch = CopyFromReg t0, Register:nxv16i1 %0
;       t13: nxv2i1 = COPY_TO_REGCLASS t2, TargetConstant:i32<5>
;         t18: i32 = MOVi32imm TargetConstant:i32<15>
;       t7: i64 = SUBREG_TO_REG TargetConstant:i64<0>, t18, TargetConstant:i32<16>
;     t12: nxv2i64 = CPY_ZPmR_D IMPLICIT_DEF:nxv2i64, t13, t7
;   t10: ch,glue = CopyToReg t0, Register:nxv2i64 $z0, t12
;   t16: i64 = TargetConstant<15>
;   t11: ch = RET_ReallyLR Register:nxv2i64 $z0, t10, t10:1