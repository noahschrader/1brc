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

int main() {
    int fd = open("../measurements.txt", O_RDONLY);
    struct stat fs;
    fstat(fd, &fs);
    char* bytes = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
    char* end_of_file = bytes + fs.st_size;
    madvise(bytes, fs.st_size, MADV_SEQUENTIAL);

    char* station;
    uint8_t length;
    int16_t temperature;
    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key = NULL;
    }

    char* c = bytes;
    while (c != end_of_file) {
        c = parse_station(c, &station, &length);
        c = parse_temperature(c, &temperature);

        struct Entry* entry = get_entry(table, station, length);
        if (entry->key == NULL) {
            entry->min = 999;
            entry->sum = 0.0;
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