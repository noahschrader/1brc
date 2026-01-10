#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

struct Metadata {
    float min;
    float sum;
    float max;
    unsigned int count;
};

struct Entry {
    char key[100];
    struct Metadata value;
};

struct Entry* get_entry(struct Entry table[], const char* key, int size) {
    for (int i = 0; i < size; ++i) {
        if (!strcmp(key, table[i].key)) {
            return &table[i];
        }
    }

    return NULL;
}

int compare_entries(const void* a, const void* b) {
    struct Entry arg1 = *(const struct Entry*)a;
    struct Entry arg2 = *(const struct Entry*)b;
    return strcmp(arg1.key, arg2.key);
}

int main(int argc, char* argv[]) {
    FILE* file = fopen("../measurements.txt", "r");

    char line[107];
    struct Entry table[10000];
    int table_size = 0;

    while (fgets(line, sizeof(line), file)) {
        char* station = strtok(line, ";");
        char* temperature = strtok(NULL, "\n");
        float f = strtof(temperature, NULL);

        struct Entry* entry = get_entry(table, station, table_size);
        if (!entry) {
            struct Entry* newEntry = &table[table_size++];
            newEntry->value.min = 99.9;
            newEntry->value.sum = 0.0;
            newEntry->value.max = -99.9;
            newEntry->value.count = 0;
            strcpy(newEntry->key, station);
            entry = newEntry;
        }

        entry->value.min = MIN(entry->value.min, f);
        entry->value.sum += f;
        entry->value.max = MAX(entry->value.max, f);
        ++entry->value.count;
    }

    qsort(table, table_size, sizeof(struct Entry), compare_entries);
    printf("{");
    for (int i = 0; i < table_size; ++i) {
        printf("%s=%.1f/%.1f/%.1f%s", table[i].key, table[i].value.min,
               table[i].value.sum / table[i].value.count, table[i].value.max,
               i < table_size - 1 ? ", " : "");
    }
    printf("}\n");
    return 0;
}