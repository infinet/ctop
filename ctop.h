#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <time.h>



int readnic(FILE *fp, char *nic, unsigned long long *rx, unsigned long long *tx);
