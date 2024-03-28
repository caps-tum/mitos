#include <pthread.h>

#ifdef USE_MPI

#include "mpi.h"
#include "Mitos.h"


// MPI hooks
int MPI_Init(int *argc, char ***argv);
int MPI_Init_thread(int *argc, char ***argv, int required, int *provided);
int MPI_Finalize();

#endif // USE_MPI

#ifdef USE_OPEN_MP
#include <omp.h>
#include <omp-tools.h>

// OMP Hooks
int ompt_initialize(ompt_function_lookup_t lookup, int initial_device_num,
                    ompt_data_t *tool_data);
void ompt_finalize(ompt_data_t *tool_data);
#endif // USE_OPEN_MP