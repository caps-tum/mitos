#include <stdio.h>
#include <stdlib.h> // for rand() and malloc()

// Define ROW_MAJOR to calculate the index in a 1D array from 2D indices
#define ROW_MAJOR(i, j, N) ((i) * (N) + (j))

// Function to initialize matrices with random values
void init_matrices(int N, double **a, double **b, double **c)
{
    int i, j;

    *a = (double*)malloc(N * N * sizeof(double));
    *b = (double*)malloc(N * N * sizeof(double));
    *c = (double*)malloc(N * N * sizeof(double));

    for (i = 0; i < N; ++i)
    {
        for (j = 0; j < N; ++j)
        {
            (*a)[ROW_MAJOR(i, j, N)] = (double)rand();
            (*b)[ROW_MAJOR(i, j, N)] = (double)rand();
            (*c)[ROW_MAJOR(i, j, N)] = 0;
        }
    }
}

int main()
{
    int m = 3000; // Define the dimensions of the matrices (m x n and n x p)
    int n = 3000;
    int p = 3000;

    double *first, *second, *result;

    // Initialize matrices with random values
    init_matrices(m, &first, &second, &result);

    // Perform matrix multiplication
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < p; j++) {
            for (int k = 0; k < n; k++) {
                result[ROW_MAJOR(i, j, p)] += first[ROW_MAJOR(i, k, n)] * second[ROW_MAJOR(k, j, p)];
            }
        }
    }

    // Display the result matrix
//    printf("Resultant matrix:\n");
   // for (int i = 0; i < m; i++) {
     //   for (int j = 0; j < p; j++) {
       //     printf("%lf ", result[ROW_MAJOR(i, j, p)]);
       // }
       // printf("\n");
   // }

    // Free the allocated memory
    free(first);
    free(second);
    free(result);

    return 0;
}
