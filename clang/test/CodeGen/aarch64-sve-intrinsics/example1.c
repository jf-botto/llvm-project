#include <arm_sve.h>

svint64_t test1 (svbool_t pg, svint64_t op2)
{
  return svmul_x (pg, svdup_s64 (0), op2);
}

svint64_t test2 (svbool_t pg, svint64_t op2)
{
  return svdiv_x (pg, svdup_s64 (0), op2);
}