#ifndef MINING_H
#define MINING_H

#include "stratum_api.h"

typedef struct {
    uint32_t starting_nonce;
    uint32_t target; // aka difficulty, aka nbits
    uint32_t ntime;
    uint32_t merkle_root_end;
    uint8_t midstate[32];
    char * jobid;
    char * extranonce2;
} bm_job;

void free_bm_job(bm_job * job);

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, const char * extranonce_2);

char * calculate_merkle_root_hash(const char * coinbase_tx, const uint8_t merkle_branches[][32], const int num_merkle_branches);

bm_job construct_bm_job(uint32_t version, const char * prev_block_hash, const char * merkle_root,
                        uint32_t ntime, uint32_t target);

void init_extranonce_2_generation(uint32_t extranonce_2_length, uint64_t starting_nonce);

char * extranonce_2_generate();

#endif // MINING_H