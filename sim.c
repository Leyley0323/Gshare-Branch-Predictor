#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h> // for error checking

//parses the hex values
static int parse(const char *hex, uint64_t *i){
    char *end;
    errno = 0;

    //convert hex to decimal
    uint64_t val = strtoull(hex, &end, 16);
    if (errno != 0) return 0;
    if (end == hex) return 0;
    *i = val;
    return 1;
}

// for incorrect input errors
static void usage() {
    fprintf(stderr, "Incorrect Input \n Please input: gshare <GPB> <RB> <Trace_File>\n");
    fprintf(stderr, "GPB = M -> number of PC bits used to index the gshare table\n");
    fprintf(stderr, "RB  = N -> number of global history bits where N <= M\n");
}

int main(int argc, char **argv){
    //checks if the correct amount of arguments have been entered
    if (argc != 5) {
        usage(argv[0]);
        return 1;
    }
    
    //checks if user inputed gshare predictor
    if (strcmp(argv[1], "gshare") != 0) {
        usage(argv[0]);
        return 1;
    }

    char *end;

     //parse M 
    long M = strtol(argv[2], &end, 10);
    if (*end != '\0' || M <= 0) {
        fprintf(stderr, "Invalid GPB: %s\n", argv[2]);
        return 1;
    }

     //parse N
    long N = strtol(argv[3], &end, 10);
    if (*end != '\0' || N < 0) {
        fprintf(stderr, "Invalid RB: %s\n", argv[3]);
        return 1;
    }

    if (N > M) {
        fprintf(stderr, "RB must be <= GPB.\n");
        return 1;
    }

    //opens trace file
    const char *trace_file = argv[4];
    FILE *f = fopen(trace_file, "r");
    if (!f) {
        perror("Error opening trace file");
        return 1;
    }

    uint64_t table_size = 1ULL << M; // calculates 2^M
    unsigned char *table = (unsigned char*) malloc(table_size);

    if (!table) {
        fprintf(stderr, "Allocation failed");
        fclose(f);
        return 1;
    }

    for (uint64_t i = 0; i < table_size; ++i) 
        table[i] = 2u;

    uint32_t GBH = 0; // GBH register
    uint32_t mask = (N == 0) ? 0u : ((1u << N) - 1u);

    uint64_t total = 0;
    uint64_t misses = 0;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), f)) {
        //trim any leading spaces
        char *p = buffer;
        while (*p && isspace((unsigned char)*p)) 
            ++p;

        //skip \n and blank lines 
        if (*p == '\0' || *p == '\n') 
            continue;

        char PC[64];
        char outcome = 0;
        if (sscanf(p, "%63s %c", PC, &outcome) < 2) {
            continue;
        }
        
        //turns all outcomes into lowercase
        if (outcome == 't' || outcome == 'T') outcome = 't';
        else if (outcome == 'n' || outcome == 'N') outcome = 'n';
        else continue; //skip all invalid lines

        uint64_t pc;
        if (!parse(PC, &pc)) { // invlaid hex
            continue;
        }

        //compute index = lower M bits of (PC >> 2) 
        uint64_t pc_shift = pc >> 2;
        uint64_t pcIndex = pc_shift & ((1ULL << M) - 1ULL);

        uint64_t index;
        if (N == 0) {
            index = pcIndex;
        } else {
            //handles xor operation
            index = pcIndex ^ ((uint64_t)GBH << (M - N));
            index &= ((1ULL << M) - 1ULL);
        }

        
        unsigned char count = table[index];

        // intially predict taken 
        int predictTaken = (count >= 2);

        int actualTaken = (outcome == 't') ? 1 : 0;

        if (predictTaken != actualTaken) 
            misses++;

        //update taken counter and ensure its in bounds (0-3)
        if (actualTaken) {
            if (count < 3) count++;
        } else {
            if (count > 0) count--;
        }
        
        table[index] = count;

        // update GBH: shift right by 1 & place outcome in MSB
        if (N > 0) {
            uint32_t outcome_bit = actualTaken ? 1u : 0u;
            GBH = ((outcome_bit << (N - 1)) | (GBH >> 1)) & mask;
        }

        total++;
    }

    fclose(f);

    double missRatio = 0.0;
    if (total > 0) 
        missRatio = (double)misses / (double)total;

    // Print output: <M> <N> <Miss Ratio>
    printf("%ld %ld %.2f%%\n", M, N, missRatio * 100.0);
    
    free(table);
    return 0;
}
