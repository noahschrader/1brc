#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#define TABLE_SIZE 50000

struct Metadata {
    long long sum;
    unsigned count;
    short min;
    short max;
};

struct Entry {
    char key[100];
    struct Metadata value;
    bool free;
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
    while (!table[index].free) {
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
    int index = 0;
    while (*c != ';') {
        station[index++] = *c++;
    }
    station[index] = '\0';
    return c + 1;
}

char* parse_temperature(char* c, short* temperature) {
    *temperature = 0;
    short sign = 1;

    if (*c == '-') {
        sign = -1;
        c++;
    }

    if (c[1] == '.') {
        *temperature = (c[0] - '0') * 10 + (c[2] - '0');
        c += 4;
    } else {
        *temperature = (c[0] - '0') * 100 + (c[1] - '0') * 10 + (c[3] - '0');
        c += 5;
    }

    *temperature *= sign;
    return c;
}

int main() {
    int fd = open("../measurements.txt", O_RDONLY);
    struct stat fs;
    fstat(fd, &fs);
    char* bytes = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
    char* end_of_file = bytes + fs.st_size;
    madvise(bytes, fs.st_size, MADV_SEQUENTIAL);

    char station[100];
    short temperature;
    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key[0] = '\0';
        table[i].free = true;
    }

    char* c = bytes;
    while (c != end_of_file) {
        c = parse_station(c, station);
        c = parse_temperature(c, &temperature);

        struct Entry* entry = get_entry(table, station);
        if (entry->free) {
            entry->value.min = 999;
            entry->value.sum = 0.0;
            entry->value.max = -999;
            entry->value.count = 0;
            strcpy(entry->key, station);
            entry->free = false;
        }

        entry->value.min = MIN(entry->value.min, temperature);
        entry->value.sum += temperature;
        entry->value.max = MAX(entry->value.max, temperature);
        ++entry->value.count;
    }

    qsort(table, TABLE_SIZE, sizeof(struct Entry), compare_entries);
    printf("{");
    for (int i = 0; i < TABLE_SIZE; ++i) {
        if (!table[i].free) {
            printf("%s=%.1f/%.1f/%.1f, ", table[i].key,
                   table[i].value.min / 10.0,
                   table[i].value.sum / 10.0 / table[i].value.count,
                   table[i].value.max / 10.0);
        }
    }
    printf("}\n");
    return 0;
}