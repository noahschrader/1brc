#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#define TABLE_SIZE 50000

struct Metadata {
    float min;
    float sum;
    float max;
    unsigned int count;
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

int main(int argc, char* argv[]) {
    FILE* file = fopen("../measurements.txt", "r");

    char line[107];
    struct Entry table[TABLE_SIZE];
    for (int i = 0; i < TABLE_SIZE; ++i) {
        table[i].key[0] = '\0';
        table[i].free = true;
    }

    while (fgets(line, sizeof(line), file)) {
        char* station = strtok(line, ";");
        char* temperature = strtok(NULL, "\n");
        float f = strtof(temperature, NULL);

        struct Entry* entry = get_entry(table, station);
        if (entry->free) {
            entry->value.min = 99.9;
            entry->value.sum = 0.0;
            entry->value.max = -99.9;
            entry->value.count = 0;
            strcpy(entry->key, station);
            entry->free = false;
        }

        entry->value.min = MIN(entry->value.min, f);
        entry->value.sum += f;
        entry->value.max = MAX(entry->value.max, f);
        ++entry->value.count;
    }

    qsort(table, TABLE_SIZE, sizeof(struct Entry), compare_entries);
    printf("{");
    for (int i = 0; i < TABLE_SIZE; ++i) {
        if (!table[i].free) {
            printf("%s=%.1f/%.1f/%.1f, ", table[i].key, table[i].value.min,
                   table[i].value.sum / table[i].value.count,
                   table[i].value.max);
        }
    }
    printf("}\n");
    return 0;
}