#include <stdbool.h>
#include <stdint.h>

struct String {
    char* data;
    uint8_t length;
};

void string_new(struct String* string, char* data, uint8_t length) {
    string->length = length;
    string->data = data;
}

char* string_data(const struct String* string) { return string->data; }

uint8_t string_length(const struct String* string) { return string->length; }

bool string_is_empty(const struct String* string) {
    return string_length(string) == 0;
}
