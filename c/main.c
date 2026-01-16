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
    for (int i = 0; i < strlen(key); ++i) {
        hash = 31 * hash + key[i];
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

long parse_temperature(char* temperature) {
    long product = 0;
    int sign = temperature[0] == '-' ? -1 : 1;

    int index = temperature[0] == '-' ? 1 : 0;
    product = product * 10 + (temperature[index++] - '0');

    index = temperature[index] == '.' ? index + 1 : index;
    product = product * 10 + (temperature[index++] - '0');

    bool has_decimal = temperature[index] == '.';
    int digit = has_decimal ? temperature[index + 1] - '0' : 0;
    int multiplier = has_decimal ? 10 : 1;
    product = product * multiplier + digit;

    return product * sign;
}

int main(int argc, char* argv[]) {
    int fd = open("../measurements.txt", O_RDONLY);
    struct stat fs;
    fstat(fd, &fs);
    char* bytes = mmap(NULL, fs.st_size, PROT_READ, MAP_SHARED, fd, 0);
    char* end_of_file = bytes + fs.st_size;
    madvise(bytes, fs.st_size, MADV_SEQUENTIAL);

    char station[100];
    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key[0] = '\0';
        table[i].free = true;
    }

    char* c = bytes;
    while (c != end_of_file) {
        c = parse_station(c, station);
        long temperature = parse_temperature(c);

        while (*c != '\n') {
            ++c;
        }
        ++c;

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