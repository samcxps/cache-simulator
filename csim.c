/**
 * csim.c:  
 * A cache simulator that can replay traces (from Valgrind) and 
 * output statistics for the number of hits, misses, and evictions.
 * The replacement policy is LRU.
 *
 * Implementation and assumptions:
 *  1. Each load/store can cause at most 1 cache miss plus a possible eviction.
 *  2. Instruction loads (I) are ignored.
 *  3. Data modify (M) is treated as a load followed by a store to the same
 *  address. Hence, an M operation can result in two cache hits, or a miss and a
 *  hit plus a possible eviction.
 */  

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

/*****************************************************************************/
/* DO NOT MODIFY THESE VARIABLES *********************************************/

//Globals set by command line args.
int b = 0;  //number of block (b) bits
int s = 0;  //number of set (s) bits
int E = 0;  //number of lines per set

//Globals derived from command line args.
int B;  //block size in bytes: B = 2^b
int S;  //number of sets: S = 2^s

//Global counters to track cache statistics in access_data().
int hit_cnt = 0; 
int miss_cnt = 0; 
int evict_cnt = 0; 

//Global to control trace output
int verbosity = 0;  //print trace if set
/*****************************************************************************/
  
//Type mem_addr_t: Use when dealing with addresses or address masks.
typedef unsigned long long int mem_addr_t; 

//Type cache_line_t: Use when dealing with cache lines.
typedef struct cache_line {                    
    char valid; 
    mem_addr_t tag; 

    // line counter var for LRU
    int count;
} cache_line_t; 

//Type cache_set_t: Use when dealing with cache sets
//Note: Each set is a pointer to a heap array of one or more cache lines.
typedef cache_line_t* cache_set_t; 
//Type cache_t: Use when dealing with the cache.
//Note: A cache is a pointer to a heap array of one or more sets.
typedef cache_set_t* cache_t; 

// Create the cache (i.e., pointer var) we're simulating.
cache_t cache;

// For LRU
int counter = 1;

/**
 * init_cache:
 * Allocates the data structure for a cache with S sets and E lines per set.
 * Initializes all valid bits and tags with 0s.
 */                    
void init_cache() {  
    // set S and B  
    S = pow(2, s);
    B = pow(2, b);

    // printf("SETS : %i\n", S);
    // printf("LINES: %i\n\n", E);

    // alloc space for cache sets
    cache = malloc(sizeof(cache_set_t) * S);
    if (cache == NULL) {
        printf("ERROR: Could not allocate space for cache.");
        exit(1);
    }

    // alloc space for lines of each cache set and init all valid bits and tag bits
    for (int set = 0; set < S; set++) {
        cache[set] = malloc(sizeof(cache_line_t) * E);
        if (cache[set] == NULL) {
            printf("ERROR: Could not allocate space for cache[%i]", set);
            exit(1);  
        }

        // init lines
        for (int line = 0; line < E; line ++) {
            cache[set][line].valid = '0';
            cache[set][line].tag = 0;
            cache[set][line].count = 0;

            // printf("SET %i LINE %i: valid=%c, tag=%llu, count=%i\n", set, line, cache[set][line].valid, cache[set][line].tag, cache[set][line].count);
        }


    }
}
  

/**
 * free_cache:
 * Frees all heap allocated memory used by the cache.
 */                    
void free_cache() {   
    for (int set = 0; set < S; set++) {
        free(cache[set]);
        cache[set] = NULL;
    }

    free(cache);
    cache = NULL;
}
   
   
/**
 * access_data:
 * Simulates data access at given "addr" memory address in the cache.
 *
 * If already in cache, increment hit_cnt
 * If not in cache, cache it (set tag), increment miss_cnt
 * If a line is evicted, increment evict_cnt
 */                    
void access_data(mem_addr_t addr) {   
    // vars for bit masks  
    mem_addr_t sBitMask;
    mem_addr_t tBitMask;

    // vars to store bits after extracting them
    mem_addr_t set;
    mem_addr_t tag;

    sBitMask = ((mem_addr_t)1 << (s)) - 1;       // Left shift by amount of s bits and subtract 1 for mask
    tBitMask = ((mem_addr_t)1 << (64-b-s)) - 1;  // Left shift by amount of t bits and subtract 1 for mask

    set = (addr >> b) & sBitMask;      // Shift s bits to front of addr then AND with sBitMask to get set
    tag = (addr >> (b+s)) & tBitMask;  // Shift t bits to front of addr then AND with tBitMask to get tag

    /*
     * Iterate over lines in given set to see if we have a hit
     */
    for (int line = 0; line < E; line++) {

        // If tag matches and line is valid we have a hit
        if (cache[set][line].tag == tag && cache[set][line].valid == '1') {
            hit_cnt++;

            cache[set][line].count = counter;
            counter++;

            // return because we have a hit
            return; 
        }
    }


    /*
     * Else we have a miss so cache it
     *
     * 2 cases for a miss
     *     1.) The set has an empty line
     *     2.) The set is full so we need to evict
     */
    miss_cnt++;

    // Variables to help decide "miss case"
    int freeLine = -1;
    int evictLine = -1;
    int minCount = INT_MAX;

    // Iterate over lines again to find either free line or evict line
    for (int line = 0; line < E; line++) {
        // If valid is 0 we store the line and break for loop because we have free line
        if (cache[set][line].valid == '0') {
            freeLine = line;
            break;
        }

        // If the loop does not break because of a found free line, we find the
        // least recently used line in the set
        if (cache[set][line].count < minCount) {
            minCount = cache[set][line].count;
            evictLine = line;
        }
    }

    // Case 1: The set has an empty line
    if (freeLine != -1) {
        cache[set][freeLine].valid = '1';       // Set valid to 1
        cache[set][freeLine].tag = tag;         // Set tag
        cache[set][freeLine].count = counter;   // Set counter
        
        counter++;

        return;
    } 
    
    // Case 2: The set is full so we need to evict
    if (evictLine != -1) { 
        evict_cnt++;

        cache[set][evictLine].valid = '1';
        cache[set][evictLine].tag = tag;
        cache[set][evictLine].count = counter;

        counter++;

        return;
    }

    // Will never reach this
    return;
}
  
  
/**
 *
 * replay_trace:
 * Replays the given trace file against the cache.
 *
 * Reads the input trace file line by line.
 * Extracts the type of each memory access : L/S/M
 * TRANSLATE each "L" as a load i.e. 1 memory access
 * TRANSLATE each "S" as a store i.e. 1 memory access
 * TRANSLATE each "M" as a load followed by a store i.e. 2 memory accesses 
 */                    
void replay_trace(char* trace_fn) {           
    char buf[1000];   
    mem_addr_t addr = 0; 
    unsigned int len = 0; 
    FILE* trace_fp = fopen(trace_fn, "r");  

    if (!trace_fp) { 
        fprintf(stderr, "%s: %s\n", trace_fn, strerror(errno)); 
        exit(1);    
    }

    while (fgets(buf, 1000, trace_fp) != NULL) {
        if (buf[1] == 'S' || buf[1] == 'L' || buf[1] == 'M') {
            sscanf(buf+3, "%llx,%u", &addr, &len); 
      
            if (verbosity)
                printf("%c %llx,%u ", buf[1], addr, len); 

            if (buf[1] == 'M') {
                access_data(addr);
                access_data(addr);
            } else if (buf[1] == 'L') {
                access_data(addr);
            } else if (buf[1] == 'S') {
                access_data(addr);
            } // ignore I instruction fetches

            if (verbosity)
                printf("\n"); 
        }
    }

    fclose(trace_fp); 
}  
  
  
/**
 * print_usage:
 * Print information on how to use csim to standard output.
 */                    
void print_usage(char* argv[]) {                 
    printf("Usage: %s [-hv] -s <num> -E <num> -b <num> -t <file>\n", argv[0]); 
    printf("Options:\n"); 
    printf("  -h         Print this help message.\n"); 
    printf("  -v         Optional verbose flag.\n"); 
    printf("  -s <num>   Number of s bits for set index.\n"); 
    printf("  -E <num>   Number of lines per set.\n"); 
    printf("  -b <num>   Number of b bits for block offsets.\n"); 
    printf("  -t <file>  Trace file.\n"); 
    printf("\nExamples:\n"); 
    printf("  linux>  %s -s 4 -E 1 -b 4 -t traces/yi.trace\n", argv[0]); 
    printf("  linux>  %s -v -s 8 -E 2 -b 4 -t traces/yi.trace\n", argv[0]); 
    exit(0); 
}  
  
  
/**
 * print_summary:
 * Prints a summary of the cache simulation statistics to a file.
 */                    
void print_summary(int hits, int misses, int evictions) {                
    printf("hits:%d misses:%d evictions:%d\n", hits, misses, evictions); 
    FILE* output_fp = fopen(".csim_results", "w"); 
    assert(output_fp); 
    fprintf(output_fp, "%d %d %d\n", hits, misses, evictions); 
    fclose(output_fp); 
}  
  
  
/**
 * main:
 * Main parses command line args, makes the cache, replays the memory accesses
 * free the cache and print the summary statistics.  
 */                    
int main(int argc, char* argv[]) {                      
    char* trace_file = NULL; 
    char c; 
    
    // Parse the command line arguments: -h, -v, -s, -E, -b, -t 
    while ((c = getopt(argc, argv, "s:E:b:t:vh")) != -1) {
        switch (c) {
            case 'b':
                b = atoi(optarg); 
                break; 
            case 'E':
                E = atoi(optarg); 
                break; 
            case 'h':
                print_usage(argv); 
                exit(0); 
            case 's':
                s = atoi(optarg); 
                break; 
            case 't':
                trace_file = optarg; 
                break; 
            case 'v':
                verbosity = 1; 
                break; 
            default:
                print_usage(argv); 
                exit(1); 
        }
    }

    //Make sure that all required command line args were specified.
    if (s == 0 || E == 0 || b == 0 || trace_file == NULL) {
        printf("%s: Missing required command line argument\n", argv[0]); 
        print_usage(argv); 
        exit(1); 
    }

    //Initialize cache.
    init_cache(); 

    //Replay the memory access trace.
    replay_trace(trace_file); 

    //Free memory allocated for cache.
    free_cache(); 

    //Print the statistics to a file.
    //DO NOT REMOVE: This function must be called for test_csim to work.
    print_summary(hit_cnt, miss_cnt, evict_cnt); 
    return 0;    
}  


// end csim.c
