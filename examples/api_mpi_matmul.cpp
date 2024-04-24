/******************************************************************************
 * * FILE: mpi_mm.c
 * * DESCRIPTION:  
 * *   MPI Matrix Multiply - C Version
 * *   In this code, the master task distributes a matrix multiply
 * *   operation to numtasks-1 worker tasks.
 * *   NOTE:  C and Fortran versions of this code differ because of the way
 * *   arrays are stored/passed.  C arrays are row-major order but Fortran
 * *   arrays are column-major order.
 * * AUTHOR: Blaise Barney. Adapted from Ros Leibensperger, Cornell Theory
 * *   Center. Converted to MPI: George L. Gusciora, MHPCC (1/95)
 * * LAST REVISED: 04/13/05
 * ******************************************************************************/
#include "mpi.h"
#include <stdio.h>
#include <stdlib.h>
#include "src/Mitos.h"
#include "src/virtual_address_writer.h"
#include <unistd.h>

#define NRA 1024                 /* number of rows in matrix A */
#define NCA 1024                 /* number of columns in matrix A */
#define NCB 1024                  /* number of columns in matrix B */
#define MASTER 0               /* taskid of first task */
#define FROM_MASTER 1          /* setting a message type */
#define FROM_WORKER 2          /* setting a message type */
double	a[NRA][NCA],           /* matrix A to be multiplied */
b[NCA][NCB],           /* matrix B to be multiplied */
c[NRA][NCB];           /* result matrix C */

thread_local static mitos_output mout;

void sample_handler(perf_event_sample *sample, void *args)
{
    LOG_HIGH("mitoshooks.cpp: sample_handler(), MPI handler sample: cpu= " << sample->cpu << " tid= "  << sample->tid);
    Mitos_write_sample(sample, &mout);
}

int main (int argc, char *argv[])
{
    int	numtasks,              /* number of tasks in partition */
    	taskid,                /* a task identifier */
        numworkers,            /* number of worker tasks */
        source,                /* task id of message source */
	    dest,                  /* task id of message destination */
	    mtype,                 /* message type */
	    rows,                  /* rows of matrix A sent to each worker */
	    averow, extra, offset, /* used to determine rows sent to each worker */
	    i, j, k, rc;           /* misc */
   
   
    
    numworkers = numtasks-1;
    MPI_Init(&argc,&argv);
    MPI_Status status;
    MPI_Comm_rank(MPI_COMM_WORLD,&taskid);
    MPI_Comm_size(MPI_COMM_WORLD,&numtasks);
    if (numtasks < 2 ) {
        fprintf(stderr,"Need at least two MPI tasks. Quitting...\n");
        MPI_Abort(MPI_COMM_WORLD, rc);
        exit(1);
    }
    numworkers = numtasks-1;

   /* Setting up mitos for every process*/ 
   long ts_output = time(NULL);
   // send timestamp from rank 0 to all others to synchronize folder prefix 
    MPI_Bcast(&ts_output, 1, MPI_LONG, 0, MPI_COMM_WORLD);
    

   /* Process specific directory name*/
    char rank_prefix[54];
    sprintf(rank_prefix, "mitos_%ld_rank_%d_", ts_output, taskid);

    char* virt_address = new char[(strlen(rank_prefix) + strlen("/tmp/") + strlen("virt_address.txt") + 1)];
    strcpy(virt_address, "/tmp/");
    strcat(virt_address, rank_prefix);
    strcat(virt_address, "virt_address.txt");
    Mitos_save_virtual_address_offset(std::string(virt_address));
    
    Mitos_create_output(&mout, rank_prefix);
    pid_t curpid = getpid();
    
    Mitos_pre_process(&mout);
    Mitos_set_pid(curpid);

    Mitos_set_handler_fn(&sample_handler,NULL);
    Mitos_set_sample_event_period(4000);
    Mitos_set_sample_latency_threshold(4);

   std::cout << "[Mitos] Begin sampler, rank: " << taskid << "\n";
   Mitos_begin_sampler();  

   /**************************** master task ************************************/
   if (taskid == MASTER)
   {
      printf("mpi_mm has started with %d tasks.\n",numtasks);
      printf("Initializing arrays...\n");
      for (i=0; i<NRA; i++)
         for (j=0; j<NCA; j++)
            a[i][j]= i+j;
      for (i=0; i<NCA; i++)
         for (j=0; j<NCB; j++)
            b[i][j]= i*j;

      /* Send matrix data to the worker tasks */
      averow = NRA/numworkers;
      extra = NRA%numworkers;
      offset = 0;
      mtype = FROM_MASTER;
      for (dest=1; dest<=numworkers; dest++)
      {
         rows = (dest <= extra) ? averow+1 : averow;
         printf("Sending %d rows to task %d offset=%d\n",rows,dest,offset);
         MPI_Send(&offset, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
         MPI_Send(&rows, 1, MPI_INT, dest, mtype, MPI_COMM_WORLD);
         MPI_Send(&a[offset][0], rows*NCA, MPI_DOUBLE, dest, mtype,
                   MPI_COMM_WORLD);
         MPI_Send(&b, NCA*NCB, MPI_DOUBLE, dest, mtype, MPI_COMM_WORLD);
         offset = offset + rows;
      }

      /* Receive results from worker tasks */
      mtype = FROM_WORKER;
      for (i=1; i<=numworkers; i++)
      {
         source = i;
         MPI_Recv(&offset, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
         MPI_Recv(&rows, 1, MPI_INT, source, mtype, MPI_COMM_WORLD, &status);
         MPI_Recv(&c[offset][0], rows*NCB, MPI_DOUBLE, source, mtype,
                  MPI_COMM_WORLD, &status);
         printf("Received results from task %d\n",source);
      }

      /* Print results */
      /*
      printf("******************************************************\n");
      printf("Result Matrix:\n");
      for (i=0; i<NRA; i++)
      {
         printf("\n");
         for (j=0; j<NCB; j++)
            printf("%6.2f   ", c[i][j]);
      }
      printf("\n******************************************************\n");
      printf ("Done.\n");
      */
   }


/**************************** worker task ************************************/
   if (taskid > MASTER)
   {
      mtype = FROM_MASTER;
      MPI_Recv(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
      MPI_Recv(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD, &status);
      MPI_Recv(&a, rows*NCA, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD, &status);
      MPI_Recv(&b, NCA*NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD, &status);

      for (k=0; k<NCB; k++)
         for (i=0; i<rows; i++)
         {
            c[i][k] = 0.0;
            for (j=0; j<NCA; j++)
               c[i][k] = c[i][k] + a[i][j] * b[j][k];
         }
      mtype = FROM_WORKER;
      MPI_Send(&offset, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
      MPI_Send(&rows, 1, MPI_INT, MASTER, mtype, MPI_COMM_WORLD);
      MPI_Send(&c, rows*NCB, MPI_DOUBLE, MASTER, mtype, MPI_COMM_WORLD);
   }

   Mitos_end_sampler();
   MPI_Barrier(MPI_COMM_WORLD);
   LOG_LOW("mitoshooks.cpp: MPI_Finalize(), Flushed raw samples, rank no.: " << taskid);
   Mitos_add_offsets(virt_address, &mout);
   
   /* Post-processing of samplers*/
   if (taskid == MASTER) {
       int ret_val = Mitos_merge_files(std::string("mitos_") + std::to_string(ts_output) + "_rank_", std::string("mitos_") + std::to_string(ts_output) + "_rank_0");
       Mitos_openFile("/proc/self/exe", &mout);
       mitos_output result_mout;
       std::string result_dir = "mitos_" + std::to_string(ts_output) + "_rank_result";
       Mitos_set_result_mout(&result_mout, result_dir.c_str());    
       Mitos_post_process("/proc/self/exe", &result_mout, result_dir);
   }
   MPI_Barrier(MPI_COMM_WORLD);
   delete[] virt_address;
   MPI_Finalize();
}
