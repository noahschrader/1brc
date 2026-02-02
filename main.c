#define _GNU_SOURCE
#include <fcntl.h>
#include <immintrin.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include "string.h"

#define TABLE_SIZE 65536
#define NUM_CHUNKS 4

struct Entry {
    char* key;
    int64_t sum;
    uint32_t count;
    uint32_t hash;
    int16_t min;
    int16_t max;
    uint8_t key_length;
    uint8_t _pad[3];
};

uint32_t hash(const char* key, uint8_t length) {
    static const uint64_t masks[9] = {
        0x0000000000000000, 0x00000000000000FF, 0x000000000000FFFF,
        0x0000000000FFFFFF, 0x00000000FFFFFFFF, 0x000000FFFFFFFFFF,
        0x0000FFFFFFFFFFFF, 0x00FFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
    };
    uint32_t hash = length;
    uint64_t* data = (uint64_t*)key;
    uint8_t len1 = length >= 8 ? 8 : length;
    uint8_t len2 = length > 8 ? (length >= 16 ? 8 : length - 8) : 0;
    uint64_t d1 = data[0] & masks[len1];
    uint64_t d2 = data[1] & masks[len2];
    hash = _mm_crc32_u64(hash, d1);
    hash = _mm_crc32_u64(hash, d2);
    if (__builtin_expect(length > 16, 0)) {
        for (int i = 16; i < length; ++i) {
            hash = _mm_crc32_u8(hash, key[i]);
        }
    }
    return hash;
}

bool key_equals(const char* key1, uint8_t key1_length, const char* key2,
                uint8_t key2_length) {
    return key1_length == key2_length && memcmp(key1, key2, key1_length) == 0;
}

struct Entry* get_entry(struct Entry table[], const char* key, uint8_t length) {
    int index = hash(key, length) & (TABLE_SIZE - 1);
    while (table[index].key != NULL) {
        if (key_equals(table[index].key, table[index].key_length, key,
                       length)) {
            return &table[index];
        }
        index = (index + 1) & (TABLE_SIZE - 1);
    }
    return &table[index];
}

void update_entry(struct Entry* entry, char* station, uint8_t length,
                  int16_t temperature) {
    if (entry->key == NULL) {
        entry->min = 999;
        entry->sum = 0;
        entry->max = -999;
        entry->count = 0;
        entry->key = station;
        entry->key_length = length;
    }

    entry->min = MIN(entry->min, temperature);
    entry->sum += temperature;
    entry->max = MAX(entry->max, temperature);
    ++entry->count;
}

int compare_entries(const void* a, const void* b) {
    struct Entry arg1 = *(const struct Entry*)a;
    struct Entry arg2 = *(const struct Entry*)b;
    if (arg1.key == NULL || arg2.key == NULL) {
        return -1;
    }
    int length = MIN(arg1.key_length, arg2.key_length);
    int cmp = memcmp(arg1.key, arg2.key, length);
    return cmp != 0 ? cmp : arg1.key_length - arg2.key_length;
}

char* parse_station(char* c, char** key, uint8_t* key_length) {
    __m256i semicolons = _mm256_set1_epi8(';');
    uint32_t mask = 0;
    int16_t length = 0;
    while (!mask) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)(c + length));
        __m256i cmp = _mm256_cmpeq_epi8(chunk, semicolons);
        mask = _mm256_movemask_epi8(cmp);
        length += 32;
    }

    uint8_t tailing_zeros = __builtin_ctz(mask);
    length += tailing_zeros - 32;
    *key = c;
    *key_length = length;

    return c + length + 1;
}

char* parse_temperature(char* c, int16_t* temperature) {
    uint64_t word = *(uint64_t*)c;
    bool is_negative = (word & 0xFF) == '-';
    word >>= is_negative * 8;

    char byte0 = (word >> 0) & 0xFF;
    char byte1 = (word >> 8) & 0xFF;
    char byte2 = (word >> 16) & 0xFF;
    char byte3 = (word >> 24) & 0xFF;

    bool is_short = byte1 == '.';
    int16_t temperature_short = (byte0 - '0') * 10 + (byte2 - '0');
    int16_t temperature_long =
        (byte0 - '0') * 100 + (byte1 - '0') * 10 + (byte3 - '0');

    int16_t result =
        is_short * temperature_short + (1 - is_short) * temperature_long;
    result = (result ^ -is_negative) + is_negative;
    *temperature = result;

    uint8_t length = 3 + (1 - is_short) + is_negative;
    return c + length + 1;
}

char* get_next_line(char* c) {
    __m256i newlines = _mm256_set1_epi8('\n');
    uint32_t mask = 0;
    while (!mask) {
        __m256i chunk = _mm256_loadu_si256((__m256i*)c);
        __m256i cmp = _mm256_cmpeq_epi8(chunk, newlines);
        mask = _mm256_movemask_epi8(cmp);
        c += 32;
    }

    uint8_t tailing_zeros = __builtin_ctz(mask);
    return c - 32 + tailing_zeros + 1;
}

int main() {
    int fd = open("measurements.txt", O_RDONLY);
    struct stat fs;
    fstat(fd, &fs);
    char* bytes = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
    char* end_of_file = bytes + fs.st_size;
    madvise(bytes, fs.st_size, MADV_SEQUENTIAL);

    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key = NULL;
    }

    size_t chunk_size = fs.st_size / NUM_CHUNKS;
    char* chunk_start[NUM_CHUNKS];
    char* chunk_end[NUM_CHUNKS];

    chunk_start[0] = bytes;
    for (int i = 1; i < NUM_CHUNKS; ++i) {
        chunk_start[i] = get_next_line(bytes + i * chunk_size);
    }
    for (int i = 0; i < NUM_CHUNKS - 1; ++i) {
        chunk_end[i] = chunk_start[i + 1];
    }
    chunk_end[NUM_CHUNKS - 1] = end_of_file;

    while (chunk_start[0] < chunk_end[0] && chunk_start[1] < chunk_end[1] &&
           chunk_start[2] < chunk_end[2] && chunk_start[3] < chunk_end[3]) {
        char *station0, *station1, *station2, *station3;
        uint8_t length0, length1, length2, length3;
        int16_t temperature0, temperature1, temperature2, temperature3;

        char* tp0 = parse_station(chunk_start[0], &station0, &length0);
        char* tp1 = parse_station(chunk_start[1], &station1, &length1);
        char* tp2 = parse_station(chunk_start[2], &station2, &length2);
        char* tp3 = parse_station(chunk_start[3], &station3, &length3);

        chunk_start[0] = parse_temperature(tp0, &temperature0);
        chunk_start[1] = parse_temperature(tp1, &temperature1);
        chunk_start[2] = parse_temperature(tp2, &temperature2);
        chunk_start[3] = parse_temperature(tp3, &temperature3);

        struct Entry* e0 = get_entry(table, station0, length0);
        struct Entry* e1 = get_entry(table, station1, length1);
        struct Entry* e2 = get_entry(table, station2, length2);
        struct Entry* e3 = get_entry(table, station3, length3);

        update_entry(e0, station0, length0, temperature0);
        update_entry(e1, station1, length1, temperature1);
        update_entry(e2, station2, length2, temperature2);
        update_entry(e3, station3, length3, temperature3);
    }

    for (int i = 0; i < NUM_CHUNKS; ++i) {
        while (chunk_start[i] < chunk_end[i]) {
            char* station;
            uint8_t length;
            int16_t temperature;

            chunk_start[i] = parse_station(chunk_start[i], &station, &length);
            chunk_start[i] = parse_temperature(chunk_start[i], &temperature);

            struct Entry* entry = get_entry(table, station, length);
            update_entry(entry, station, length, temperature);
        }
    }

    qsort(table, TABLE_SIZE, sizeof(struct Entry), compare_entries);
    printf("{");
    for (int i = 0; i < TABLE_SIZE; ++i) {
        if (table[i].key != NULL) {
            printf("%.*s=%.1f/%.1f/%.1f, ", table[i].key_length, table[i].key,
                   table[i].min / 10.0, table[i].sum / 10.0 / table[i].count,
                   table[i].max / 10.0);
        }
    }
    printf("}\n");
    return 0;
}