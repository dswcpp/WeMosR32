#include "stock_quote_parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STOCK_QUOTE_MAX_FIELDS 40u

static void copy_text(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0u) {
        return;
    }
    (void)snprintf(destination, size, "%s", source == NULL ? "" : source);
}

static bool is_six_digit_code(const char *value)
{
    size_t length = value == NULL ? 0u : strlen(value);

    if (length != 6u) {
        return false;
    }
    for (size_t index = 0u; index < length; index++) {
        if (!isdigit((unsigned char)value[index])) {
            return false;
        }
    }
    return true;
}

bool stock_symbol_normalize(const char *input,
                            char output[STOCK_SYMBOL_MAX_LEN + 1u])
{
    char compact[STOCK_SYMBOL_MAX_LEN + 1u];
    size_t length = 0u;
    const char *body;
    const char *market;

    if (input == NULL || output == NULL) {
        return false;
    }
    while (*input != '\0' && isspace((unsigned char)*input)) {
        input++;
    }
    for (; *input != '\0' && !isspace((unsigned char)*input); input++) {
        if (length >= STOCK_SYMBOL_MAX_LEN ||
            !isalnum((unsigned char)*input)) {
            return false;
        }
        compact[length++] = (char)tolower((unsigned char)*input);
    }
    if (length < 5u) {
        return false;
    }
    compact[length] = '\0';

    if (strncmp(compact, "sh", 2u) == 0 ||
        strncmp(compact, "sz", 2u) == 0 ||
        strncmp(compact, "bj", 2u) == 0 ||
        strncmp(compact, "hk", 2u) == 0 ||
        strncmp(compact, "us", 2u) == 0) {
        market = compact;
        body = compact + 2;
        if (*body == '\0') {
            return false;
        }
        if ((market[0] == 's' || market[0] == 'b') &&
            !is_six_digit_code(body)) {
            return false;
        }
        if (strlen(compact) > STOCK_SYMBOL_MAX_LEN) {
            return false;
        }
        if ((market[0] == 's' || market[0] == 'b') &&
            body[0] == '5') {
            market = "sh";
        }
        memcpy(output, market, 2u);
        memcpy(output + 2u, body, strlen(body) + 1u);
        return true;
    }

    if (!is_six_digit_code(compact)) {
        return false;
    }
    if (compact[0] == '5' || compact[0] == '6') {
        market = "sh";
    } else if (compact[0] == '4' || compact[0] == '8') {
        market = "bj";
    } else {
        market = "sz";
    }
    if (strlen(market) + strlen(compact) > STOCK_SYMBOL_MAX_LEN) {
        return false;
    }
    memcpy(output, market, 2u);
    memcpy(output + 2u, compact, strlen(compact) + 1u);
    return true;
}

static bool parse_double_field(const char *value, double *output)
{
    char *end = NULL;
    double parsed;

    if (value == NULL || output == NULL || *value == '\0') {
        return false;
    }
    parsed = strtod(value, &end);
    if (end == value || *end != '\0') {
        return false;
    }
    *output = parsed;
    return true;
}

static bool parse_quote_record(char *record,
                               stock_quote_t *quotes, size_t capacity,
                               size_t *quote_count)
{
    char *separator = strstr(record, "=\"");
    char *record_end;
    char *fields[STOCK_QUOTE_MAX_FIELDS] = {0};
    size_t field_count = 0u;
    stock_quote_t quote = {0};
    char normalized[STOCK_SYMBOL_MAX_LEN + 1u];
    char symbol_source[STOCK_SYMBOL_MAX_LEN + 1u];

    if (separator == NULL || *quote_count >= capacity) {
        return false;
    }
    record_end = strchr(separator + 2u, '"');
    if (record_end == NULL) {
        return false;
    }
    *record_end = '\0';

    if (record[0] == 'v' && record[1] == '_' &&
        separator > record + 2) {
        size_t symbol_length = (size_t)(separator - (record + 2));

        if (symbol_length == 0u || symbol_length > STOCK_SYMBOL_MAX_LEN) {
            return false;
        }
        memcpy(symbol_source, record + 2, symbol_length);
        symbol_source[symbol_length] = '\0';
        if (!stock_symbol_normalize(symbol_source, normalized)) {
            return false;
        }
    } else {
        return false;
    }

    /* Tencent responses contain empty fields, so preserve every separator. */
    for (char *field = separator + 2u;
         field_count < STOCK_QUOTE_MAX_FIELDS;
         field_count++) {
        char *next = strchr(field, '~');

        fields[field_count] = field;
        if (next == NULL) {
            field_count++;
            break;
        }
        *next = '\0';
        field = next + 1u;
    }
    if (field_count < 35u) {
        return false;
    }

    if (!parse_double_field(fields[3], &quote.price) ||
        !parse_double_field(fields[4], &quote.previous_close) ||
        !parse_double_field(fields[5], &quote.open) ||
        !parse_double_field(fields[6], &quote.volume_lots) ||
        !parse_double_field(fields[31], &quote.change) ||
        !parse_double_field(fields[32], &quote.change_percent) ||
        !parse_double_field(fields[33], &quote.high) ||
        !parse_double_field(fields[34], &quote.low)) {
        return false;
    }
    if (field_count > 37u) {
        double turnover_wan = 0.0;

        if (parse_double_field(fields[37], &turnover_wan) &&
            turnover_wan > 0.0) {
            quote.turnover_cny = turnover_wan * 10000.0;
        }
    }

    copy_text(quote.symbol, sizeof(quote.symbol), normalized);
    copy_text(quote.code, sizeof(quote.code), normalized + 2u);
    copy_text(quote.updated_at, sizeof(quote.updated_at), fields[30]);
    quote.valid = quote.price > 0.0;
    if (!quote.valid) {
        return false;
    }
    quotes[(*quote_count)++] = quote;
    return true;
}

bool stock_quote_parser_parse(const char *response, stock_quote_t *quotes,
                              size_t capacity, size_t *quote_count)
{
    char *copy;
    size_t response_length;
    char *save_pointer = NULL;
    size_t parsed = 0u;
    bool found = false;

    if (response == NULL || quotes == NULL || quote_count == NULL ||
        capacity == 0u) {
        return false;
    }
    *quote_count = 0u;
    response_length = strlen(response);
    copy = malloc(response_length + 1u);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, response, response_length + 1u);

    for (char *line = strtok_r(copy, ";\r\n", &save_pointer);
         line != NULL;
         line = strtok_r(NULL, ";\r\n", &save_pointer)) {
        while (*line != '\0' && isspace((unsigned char)*line)) {
            line++;
        }
        if (strncmp(line, "v_", 2u) == 0 &&
            parse_quote_record(line, quotes, capacity, &parsed)) {
            found = true;
        }
    }

    free(copy);
    *quote_count = parsed;
    return found;
}
