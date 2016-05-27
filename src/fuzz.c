#include <stdio.h>
#include "flatjson.h"
#include "validate.h"

int main(void) {
    char buf[2048];
    const ssize_t n_read = fread(buf, 1, sizeof(buf)-1, stdin);
    if(n_read < 0) { return 1; }
    buf[n_read] = '\0';

    char term[2048];
    char const* cursor = buf;
    while((cursor = flatjson_next(cursor, term, sizeof(term), NULL)) != NULL) {
        validate_iface(term);
        validate_stanza(term);
    }

    return 0;
}
