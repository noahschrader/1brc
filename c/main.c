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

#define TABLE_SIZE 50000

struct Metadata {
    int64_t sum;
    uint32_t count;
    int16_t min;
    int16_t max;
};

struct Entry {
    char key[100];
    struct Metadata value;
};

int hash(const char* key) {
    int hash = 31;
    while (*key) {
        hash = 31 * hash + *key++;
    }
    return hash % TABLE_SIZE;
}

struct Entry* get_entry(struct Entry table[], const char* key) {
    int index = hash(key);
    while (table[index].key[0] != '\0') {
        if (strcmp(key, table[index].key) == 0) {
            return &table[index];
        }
        ++index;
        if (index >= TABLE_SIZE) {
            index = 0;
        }
    }
    return &table[index];
}

int compare_entries(const void* a, const void* b) {
    struct Entry arg1 = *(const struct Entry*)a;
    struct Entry arg2 = *(const struct Entry*)b;
    return strcmp(arg1.key, arg2.key);
}

char* parse_station(char* c, char* station) {
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
    memcpy(station, c, length);
    station[length] = '\0';

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

    char station[100];
    int16_t temperature;
    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key[0] = '\0';
    }

    char* c = bytes;
    while (c != end_of_file) {
        c = parse_station(c, station);
        c = parse_temperature(c, &temperature);

        struct Entry* entry = get_entry(table, station);
        if (entry->key[0] == '\0') {
            entry->value.min = 999;
            entry->value.sum = 0.0;
            entry->value.max = -999;
            entry->value.count = 0;
            strcpy(entry->key, station);
        }

        entry->value.min = MIN(entry->value.min, temperature);
        entry->value.sum += temperature;
        entry->value.max = MAX(entry->value.max, temperature);
        ++entry->value.count;
    }

    qsort(table, TABLE_SIZE, sizeof(struct Entry), compare_entries);
    printf("{");
    for (int i = 0; i < TABLE_SIZE; ++i) {
        if (table[i].key[0] != '\0') {
            printf("%s=%.1f/%.1f/%.1f, ", table[i].key,
                   table[i].value.min / 10.0,
                   table[i].value.sum / 10.0 / table[i].value.count,
                   table[i].value.max / 10.0);
        }
    }
    printf("}\n");
    return 0;
}