#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "parse.h"
#include "validate.h"

int tests_passed = -1;
static void __assert(const char* msg, const char* func, int line) {
    fprintf(stderr, "Failed: %s:%d\n    %s\n", func, line, msg);
    fprintf(stderr, "Tests passed: %d\n", tests_passed);
    exit(1);
}

#define assert(message, test) do { if(!(test)) __assert(message, __func__, __LINE__); } while(0)
#define test() do { tests_passed += 1; fprintf(stderr, "====%s====\n", __func__); } while(0)

static void test_chomp(void) {
    test();
    
    char buf[20];
    strlcpy(buf, "  foo  \t", sizeof(buf));
    assert("", strcmp(chomp(buf), "foo") == 0);
}

static void test_unescape_simple(void) {
    test();

    char buf[100];
    char const* result = unescape("foo bar", buf, sizeof(buf));
    assert("", result != NULL);
    assert("", strcmp("foo", buf) == 0);

    result = unescape(result, buf, sizeof(buf));
    assert("", result == NULL);
    assert("", strcmp("bar", buf) == 0);
}

static void test_unescape_overflow(void) {
    test();

    char buf[5];
    char const* result = unescape("kitt", buf, sizeof(buf));
    assert("", result == NULL);
    assert("", strcmp("kitt", buf) == 0);

    result = unescape("kitty", buf, sizeof(buf));
    assert("", result == NULL);
    assert("", strcmp("kitt", buf) == 0);
}

static void test_unescape_escapes(void) {
    test();

    char buf[100];
    char const* result = unescape("f\\no\\\\o\\ bar", buf, sizeof(buf));
    assert("", result == NULL);
    assert("", strcmp("f\no\\o bar", buf) == 0);
}

static void test_unescape_null(void) {
    test();

    char buf[10];
    buf[0] = 'a';
    char const* result = unescape(NULL, buf, sizeof(buf));
    assert("", result == NULL);
    assert("", buf[0] == '\0');
    
    result = unescape(NULL, NULL, 0);
    assert("", result == NULL);

    result = unescape("foo bar", NULL, 0);
    assert("", result == NULL);
}

static void test_escape_simple(void) {
    test();

    char buf[100];
    assert("", escape("foo ba\nr", buf, sizeof(buf)) != NULL);
    assert("", strcmp("foo\\ ba\\nr", buf) == 0);
}

static void test_escape_overflow(void) {
    test();

    char buf[5];
    assert("", escape("foob", buf, sizeof(buf)) == NULL);
}

static void test_validate_iface(void) {
    test();

    assert("", validate_iface("em0"));
    assert("", validate_iface("em"));
    assert("", !validate_iface(".badvalue"));
}

static void test_validate_stanza(void) {
    test();

    assert("", validate_stanza("inet 192.168.1.5 255.255.255.0 192.168.1.255"));
    assert("", validate_stanza("inet6 2001:0db8:::::: ::::90a::: :::0db::::"));
    assert("", validate_stanza("dhcp"));
    assert("", validate_stanza("rtsol"));
    assert("", !validate_stanza("rtsl"));
    assert("", !validate_stanza("!run /bin/sh"));
    assert("", !validate_stanza("inet :::: :::: ::::"));
    assert("", !validate_stanza("inet6 200g:0db8:::::: ::::90a::: :::0db::::"));
}

static void test_parse_ifconfig_kv(void) {
    test();

    assert("", parse_ifconfig_kv("\tstatus: inactive", NULL, NULL));
    assert("", parse_ifconfig_kv("\tinet 192.168.1.2", NULL, NULL));
    assert("", !parse_ifconfig_kv("inet 192.168.1.2", NULL, NULL));
}

static void test_iface_is_pseudo(void) {
    test();

    char pseudo[200];
    strlcpy(pseudo, "bridge carp enc", sizeof(pseudo));

    assert("", !iface_is_pseudo("em0", pseudo));
    assert("", !iface_is_pseudo("em", pseudo));
    assert("", iface_is_pseudo("enc0", pseudo));
    assert("", iface_is_pseudo("bridge", pseudo));
}

static void run_tests(void) {
    test_chomp();

    test_unescape_simple();
    test_unescape_overflow();
    test_unescape_escapes();
    test_unescape_null();

    test_escape_simple();
    test_escape_overflow();

    test_validate_iface();
    test_validate_stanza();

    test_parse_ifconfig_kv();
    test_iface_is_pseudo();

    tests_passed += 1;
}

int main(void) {
    run_tests();
    printf("Tests passed: %d\n", tests_passed);

    return 0;
}
