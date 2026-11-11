#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "stock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool stock_symbol_normalize(const char *input,
                            char output[STOCK_SYMBOL_MAX_LEN + 1u]);

/*
 * Parses the plain-text Tencent quote response. Parsed quotes use the
 * normalized requested symbol and contain numeric fields only. The response
 * name field is intentionally ignored because 1.3-inch firmware fonts do not
 * contain Chinese glyphs.
 */
bool stock_quote_parser_parse(const char *response, stock_quote_t *quotes,
                              size_t capacity, size_t *quote_count);

#ifdef __cplusplus
}
#endif
