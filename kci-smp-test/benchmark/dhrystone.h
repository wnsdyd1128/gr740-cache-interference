#ifndef __DHRYSTONE_H__
#define __DHRYSTONE_H__

#include <stdint.h>

/* Forward declaration for task context (defined in experiment_common.h) */
typedef struct dhrystone_context_t dhrystone_context_t;

/*=====================================================================
 * Dhrystone data type definitions
 *=====================================================================*/
#define DHRYSTONE_SIZE 50
#define Null 0
#define true 1
#define false 0

typedef int One_Thirty;
typedef int One_Fifty;
typedef char Capital_Letter;
typedef int Boolean;
typedef char Str_30[31];
typedef int Arr_1_Dim[DHRYSTONE_SIZE];
typedef int Arr_2_Dim[DHRYSTONE_SIZE][DHRYSTONE_SIZE];

typedef enum
{
  Ident_1,
  Ident_2,
  Ident_3,
  Ident_4,
  Ident_5
} Enumeration;

typedef struct record
{
  struct record * Ptr_Comp;
  Enumeration Discr;
  union
  {
    struct
    {
      Enumeration Enum_Comp;
      int Int_Comp;
      char Str_Comp[31];
    } var_1;
    struct
    {
      Enumeration E_Comp_2;
      char Str_2_Comp[31];
    } var_2;
    struct
    {
      char Ch_1_Comp;
      char Ch_2_Comp;
    } var_3;
  } variant;
} Rec_Type, *Rec_Pointer;

/*=====================================================================
 * Memory operation type
 * Moved here from experiment_common.h so dhrystone_config_t can
 * reference it without a circular include.
 *=====================================================================*/
typedef enum
{
  LOAD_OP = 0,  /* load-only */
  STORE_OP = 1, /* store-only */
  RMW_OP = 2,   /* read-modify-write */
} op_type_t;

/*=====================================================================
 * Dhrystone workload configuration
 *
 *  arr_dim        – one side of the Arr_2_Dim square; working-set =
 *                   arr_dim^2 * sizeof(int).  Default (0) → 50 (10 KiB).
 *  num_iterations – number of dhrystone inner-loop iterations per call
 *=====================================================================*/
typedef struct
{
  int arr_dim;
  int num_iterations;
} dhrystone_config_t;

/*=====================================================================
 * API
 *  dhrystone_init        – allocate working_set_buf, initialise record
 *                          pointers and seed arrays.  Must be called
 *                          once before run_dhrystone_workload.
 *  run_dhrystone_workload – execute one invocation of the configured
 *                          workload (dhrystone loop + WS buffer sweep).
 *=====================================================================*/
void dhrystone_init(dhrystone_context_t * ctx);
void run_dhrystone_workload(dhrystone_context_t * ctx);

#endif /* __DHRYSTONE_H__ */
