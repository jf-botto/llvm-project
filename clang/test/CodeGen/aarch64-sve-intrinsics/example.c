#include <arm_sve.h>

// svint64_t test1 (svbool_t pg)
// {
//   return svmul_x (pg, svdup_s64 (5), svdup_s64 (3));
// }

//   %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
//   %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.mul.u.nxv2i64(<vscale x 2 x i1> %0, <vscale x 2 x i64> shufflevector (<vscale x 2 x i64> insertelement (<vscale x 2 x i64> poison, i64 5, i64 0), <vscale x 2 x i64> poison, <vscale x 2 x i32> zeroinitializer), <vscale x 2 x i64> shufflevector (<vscale x 2 x i64> insertelement (<vscale x 2 x i64> poison, i64 3, i64 0), <vscale x 2 x i64> poison, <vscale x 2 x i32> zeroinitializer))

svint64_t test2 (svbool_t pg)
{
  return svdup_s64_x(pg, 15);
}

//   %0 = tail call <vscale x 2 x i1> @llvm.aarch64.sve.convert.from.svbool.nxv2i1(<vscale x 16 x i1> %pg)
//   %1 = tail call <vscale x 2 x i64> @llvm.aarch64.sve.dup.nxv2i64(<vscale x 2 x i64> undef, <vscale x 2 x i1> %0, i64 15)

// svint64_t test2 (svbool_t pg)
// {
//   return svdiv_x (pg, svdup_s64 (5), svdup_s64 (3));
// }