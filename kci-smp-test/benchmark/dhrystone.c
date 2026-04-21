#include "dhrystone.h"

#include <rtems.h>
#include <stdlib.h>
#include <string.h>

#include "experiment_common.h"

/*=====================================================================
 * Context-local re-implementations of the dhrystone Proc_x/Func_x
 * routines.  These mirror the logic in dhrystone/dhry_1.c and
 * dhrystone/dhry_2.c exactly, but operate on dhrystone_context_t
 * fields instead of file-scope globals, making the workload safe to
 * run concurrently on different CPUs.
 *=====================================================================*/

static Enumeration dhry_func_1(Capital_Letter ch1, Capital_Letter ch2)
{
  Capital_Letter loc1 = ch1;
  Capital_Letter loc2 = loc1;
  if (loc2 != ch2)
    return Ident_1;
  return Ident_2;
  /* NOTE: the original Func_1 else-branch sets Ch_1_Glob = loc1, but this
   * branch is never reached in standard dhrystone (all call sites pass
   * characters that differ), so the side-effect is omitted here. */
}

static Boolean dhry_func_3(Enumeration e)
{
  return (e == Ident_3) ? true : false;
}

static Boolean dhry_func_2(Str_30 s1, Str_30 s2)
{
  One_Thirty int_loc = 2;
  Capital_Letter ch_loc = '\0';

  while (int_loc <= 2)
  {
    if (dhry_func_1(s1[int_loc], s2[int_loc + 1]) == Ident_1)
    {
      ch_loc = 'A';
      int_loc += 1;
    }
  }
  if (ch_loc >= 'W' && ch_loc < 'Z') int_loc = 7;
  if (ch_loc == 'R') return true;
  if (strcmp(s1, s2) > 0)
  {
    int_loc += 7;
    return true;
  }
  return false;
}

static void dhry_proc_7(One_Fifty v1, One_Fifty v2, One_Fifty * ref)
{
  One_Fifty loc = v1 + 2;
  *ref = v2 + loc;
}

static void dhry_proc_8(dhrystone_context_t * ctx, int v1, int v2)
{
  One_Fifty loc = (One_Fifty)v1 + 5;

  ctx->arr_1_glob[loc] = v2;
  ctx->arr_1_glob[loc + 1] = ctx->arr_1_glob[loc];
  ctx->arr_1_glob[loc + 30] = loc;

  for (One_Fifty idx = loc; idx <= loc + 1; ++idx)
    ctx->arr_2_glob[loc][idx] = loc;

  ctx->arr_2_glob[loc][loc - 1] += 1;
  ctx->arr_2_glob[loc + 20][loc] = ctx->arr_1_glob[loc];

  ctx->int_glob = 5;
}

static void dhry_proc_6(dhrystone_context_t * ctx, Enumeration val,
                        Enumeration * ref)
{
  *ref = val;
  if (!dhry_func_3(val)) *ref = Ident_4;
  switch (val)
  {
    case Ident_1:
      *ref = Ident_1;
      break;
    case Ident_2:
      *ref = (ctx->int_glob > 100) ? Ident_1 : Ident_4;
      break;
    case Ident_3:
      *ref = Ident_2;
      break;
    case Ident_4:
      break;
    case Ident_5:
      *ref = Ident_3;
      break;
  }
}

static void dhry_proc_3(dhrystone_context_t * ctx, Rec_Pointer * ref)
{
  if (ctx->ptr_glob != Null) *ref = ctx->ptr_glob->Ptr_Comp;
  dhry_proc_7(10, ctx->int_glob, &ctx->ptr_glob->variant.var_1.Int_Comp);
}

static void dhry_proc_1(dhrystone_context_t * ctx)
{
  Rec_Pointer val_par = ctx->ptr_glob;
  Rec_Pointer next_rec = val_par->Ptr_Comp;

  *val_par->Ptr_Comp = *ctx->ptr_glob;
  val_par->variant.var_1.Int_Comp = 5;
  next_rec->variant.var_1.Int_Comp = val_par->variant.var_1.Int_Comp;
  next_rec->Ptr_Comp = val_par->Ptr_Comp;

  dhry_proc_3(ctx, &next_rec->Ptr_Comp);

  if (next_rec->Discr == Ident_1)
  {
    next_rec->variant.var_1.Int_Comp = 6;
    dhry_proc_6(ctx, val_par->variant.var_1.Enum_Comp,
                &next_rec->variant.var_1.Enum_Comp);
    next_rec->Ptr_Comp = ctx->ptr_glob->Ptr_Comp;
    dhry_proc_7(next_rec->variant.var_1.Int_Comp, 10,
                &next_rec->variant.var_1.Int_Comp);
  }
  else
  {
    *val_par = *val_par->Ptr_Comp;
  }
}

static void dhry_proc_2(dhrystone_context_t * ctx, One_Fifty * ref)
{
  One_Fifty loc = *ref + 10;
  Enumeration e_loc = Ident_4;

  do
  {
    if (ctx->ch_1_glob == 'A')
    {
      loc -= 1;
      *ref = loc - ctx->int_glob;
      e_loc = Ident_1;
    }
  } while (e_loc != Ident_1);
}

/*=====================================================================
 * Public API
 *=====================================================================*/

void dhrystone_init(dhrystone_context_t * ctx)
{
  ctx->ptr_glob = (Rec_Pointer)malloc(sizeof(Rec_Type));
  ctx->next_ptr_glob = (Rec_Pointer)malloc(sizeof(Rec_Type));

  ctx->ptr_glob->Ptr_Comp = ctx->next_ptr_glob;
  ctx->ptr_glob->Discr = Ident_1;
  ctx->ptr_glob->variant.var_1.Enum_Comp = Ident_3;
  ctx->ptr_glob->variant.var_1.Int_Comp = 40;
  strcpy(ctx->ptr_glob->variant.var_1.Str_Comp,
         "DHRYSTONE PROGRAM, SOME STRING");
  strcpy(ctx->str_1_loc, "DHRYSTONE PROGRAM, 1'ST STRING");

  ctx->arr_dim = (ctx->config.arr_dim > 0) ? ctx->config.arr_dim : 50;
  ctx->arr_2_glob[8][7] = 10;
  ctx->throughput = 0;
}

void run_dhrystone_workload(dhrystone_context_t * ctx)
{
  int num_iters =
    ctx->config.num_iterations > 0 ? ctx->config.num_iterations : 1;

  for (int run = 1; run <= num_iters; ++run)
  {
    One_Fifty int_1_loc;
    One_Fifty int_2_loc;
    One_Fifty int_3_loc;
    char ch_index;
    Enumeration enum_loc;
    Str_30 str_2_loc;

    /* Proc_5 */
    ctx->ch_1_glob = 'A';
    ctx->bool_glob = false;

    /* Proc_4 */
    Boolean bool_loc = (ctx->ch_1_glob == 'A');
    ctx->bool_glob = bool_loc | ctx->bool_glob;
    ctx->ch_2_glob = 'B';

    int_1_loc = 2;
    int_2_loc = 3;
    strcpy(str_2_loc, "DHRYSTONE PROGRAM, 2'ND STRING");
    enum_loc = Ident_2;
    ctx->bool_glob = !dhry_func_2(ctx->str_1_loc, str_2_loc);

    while (int_1_loc < int_2_loc)
    {
      int_3_loc = 5 * int_1_loc - int_2_loc;
      dhry_proc_7(int_1_loc, int_2_loc, &int_3_loc);
      int_1_loc += 1;
    }

    dhry_proc_8(ctx, int_1_loc, int_3_loc); /* sets ctx->int_glob = 5 */

    dhry_proc_1(ctx);

    for (ch_index = 'A'; ch_index <= ctx->ch_2_glob; ++ch_index)
    {
      if (enum_loc == dhry_func_1(ch_index, 'C'))
      {
        dhry_proc_6(ctx, Ident_1, &enum_loc);
        strcpy(str_2_loc, "DHRYSTONE PROGRAM, 3'RD STRING");
        int_2_loc = run;
        ctx->int_glob = run;
      }
    }

    int_2_loc = int_2_loc * int_1_loc;
    int_1_loc = int_2_loc / int_3_loc;
    int_2_loc = 7 * (int_2_loc - int_3_loc) - int_1_loc;

    dhry_proc_2(ctx, &int_1_loc);

    ctx->throughput = run;
  }

}
