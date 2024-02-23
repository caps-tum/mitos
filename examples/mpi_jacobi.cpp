/* Jacobi iteration using MPI

    mpicc -o mpi_jacobi mpi_jacobi.c
    mpirun -np <numWorkers+1> mpi_jacobi <gridSize> <iters>   
    Example usage:
    mpirun -np  4 ./mpi_jacobi 60 100   */

#include <mpi.h>
#include <stdio.h>
#include <math.h>
//#include "Mitos.h"

#define MAXGRID 258     /* maximum grid size (real points plus edges) */
#define COORDINATOR 0   /* process number of the Coordinator */

static void Coordinator(int,int,int);
static void Worker(int,int,int,int,int);


/* main() -- initialize MPI, then become one of the processes */
/* lam initiates <numWorkers+1> instances of this routine */
void call_me(void);
int main(int argc, char *argv[]) {
  int myid;
  int numWorkers, gridSize;  /* assume gridSize is multiple of numWorkers */
  int stripSize;             /* gridSize/numWorkers             */
  int numIters;              /* number of iterations to execute */

  //call_me();
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);  /* what is my id (rank)? */
  MPI_Comm_size(MPI_COMM_WORLD, &numWorkers);  /* how many processes? */
  numWorkers--;   /* one coordinator, the other processes are workers */

  /* get command-line arguments and do a simple error check */
  gridSize = atoi(argv[1]);
  numIters = atoi(argv[2]);
  stripSize = gridSize/numWorkers;
  if (gridSize%numWorkers != 0) {
    printf("grid size must be a multiple of number of workers\n");
    exit(1);
  }

  /* become one of the actual processes, depending on my id */
  if (myid == 0) {
    printf("1 Coordinator and %d Workers\n", numWorkers);
    printf("  gridSize:  %d\n  stripSize:  %d\n  numIters:  %d\n",
       gridSize, stripSize, numIters);
    Coordinator(numWorkers, stripSize, gridSize);
  } else {
    Worker(myid, numWorkers, stripSize, gridSize, numIters);
  }

  MPI_Finalize();  /* clean up MPI */
}


/* gather and print results from Workers */
static void Coordinator(int numWorkers, int stripSize, int gridSize) {
  double grid[MAXGRID][MAXGRID];
  int i, j, startrow, endrow;
  int workerid;
  MPI_Status status;
  FILE *results;
  double mydiff = 0.0, maxdiff = 0.0;
  
  for (workerid = 1; workerid <= numWorkers; workerid++) {
    startrow = (workerid-1)*stripSize + 1;
    endrow = startrow + stripSize - 1;
    for (i = startrow; i <= endrow; i++) {
        MPI_Recv(&grid[i][1], gridSize, MPI_DOUBLE, workerid, 0,
            MPI_COMM_WORLD, &status);
    }
    printf("got results from worker %d\n", workerid);
  }

  MPI_Reduce(&mydiff, &maxdiff, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  printf("global maxdiff is %f\n", maxdiff);

  /* output the results to file "results" */
  results = fopen("results", "w");
  for (i = 1; i <= gridSize; i++) {
    for (j = 1; j <= gridSize; j++) {
      fprintf(results, "%f ", grid[i][j]);
    }
    fprintf(results, "\n");
  }
}


/* Each Worker computes values in one strip, communicating with
  neighboring workers until the specified number of iterations.
  Each then sends its results to the Coordinator for printing.
 */

static void Worker(int myid, int numWorkers, int stripSize,
                   int gridSize, int numIters) {
  double grid [2][MAXGRID][MAXGRID];
  int i, j, iters;
  int current = 0, next = 1;   /* current and next iteration indices */
  int left = 0, right = 0;    /* neighboring strips above and below */
  MPI_Status status;
  double mydiff = 0.0, maxdiff, temp;

  /* set all points to 0s, then left and right edges of grid to 1s
     first worker sets top row to 1s; last worker sets bottom row to 1s */

  for (i = 0; i <= stripSize+1; i++)
    for (j = 0; j <= gridSize+1; j++) {
      grid[current][i][j] = 0.0;
      grid[next][i][j] = 0.0;
    }
  for (i = 0; i <= stripSize+1; i++) {
    grid[current][i][0] = 1.0;
    grid[current][i][gridSize+1] = 1.0;
    grid[next][i][0] = 1.0;
    grid[next][i][gridSize+1] = 1.0;
  }
  if (myid == 1)
    for (j = 0; j <= gridSize+1; j++) {
      grid[current][0][j] = 1.0;
      grid[next][0][j] = 1.0;
    }
  if (myid == numWorkers)
    for (j = 0; j <= gridSize+1; j++) {
      grid[current][stripSize+1][j] = 1.0;
      grid[next][stripSize+1][j] = 1.0;
    }

  /* determine neighbors */
  if (myid > 1)
    left = (myid-2)%numWorkers + 1;
  if (myid < numWorkers)
    right = myid%numWorkers + 1;

  printf("Worker %d initialized; left is worker %d and right is worker %d\n", 
      myid, left, right);

  /* do the actual computation */
  for (iters = 1; iters <= numIters; iters++) {
    /* exchange my boundaries with my neighbors, in a ring */
    if (right != 0)
        MPI_Send(&grid[next][stripSize][1], gridSize, MPI_DOUBLE, right, 0,
                    MPI_COMM_WORLD);
    if (left != 0)
        MPI_Send(&grid[next][1][1], gridSize, MPI_DOUBLE, left, 0,
                    MPI_COMM_WORLD);
    if (left != 0)
        MPI_Recv(&grid[next][0][1], gridSize, MPI_DOUBLE, left, 0,
                    MPI_COMM_WORLD, &status);
    if (right != 0)
        MPI_Recv(&grid[next][stripSize+1][1], gridSize, MPI_DOUBLE, right, 0,
                    MPI_COMM_WORLD, &status);

    /* update my points */
    for (i = 1; i <= stripSize; i++) {
      for (j = 1; j <= gridSize; j++) {
        grid[next][i][j] = (grid[current][i-1][j] + grid[current][i+1][j] +
               grid[current][i][j-1] + grid[current][i][j+1]) / 4;
      }
    }

    /* swap roles of grids */
    current = next;  next = 1-next;
  }

  /* send results of my current strip to the coordinator */
  for (i = 1; i <= stripSize; i++) {
      MPI_Send(&grid[current][i][1], gridSize, MPI_DOUBLE,
            COORDINATOR, 0, MPI_COMM_WORLD);
  }

  /* compute maximum difference and reduce it with Coordinator */
  for (i = 1; i <= stripSize; i++)
    for (j = 1; j <= gridSize; j++) {
      temp = fabs(grid[next][i][j] - grid[current][i][j]);
      if (temp > mydiff)
        mydiff = temp;
    }
  MPI_Reduce(&mydiff, &maxdiff, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  printf("maxdiff of worker %d is %f\n", myid, mydiff);

}