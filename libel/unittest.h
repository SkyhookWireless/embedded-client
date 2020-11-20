#ifndef UNITTEST_H
#define UNITTEST_H

#define POSIX

#ifdef POSIX
#define _POSIX_C_SOURCE 200809L // for strsignal()
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define SKY_LIBEL
#include "libel.h"

#define UNITTESTS

#define TEST_DEVICE_ID "123456123456112233445566"
#define TEST_PARTNER_ID 2
#define TEST_KEY "000102030405060708090a0b0c0d0e0f"

// ANSI Colors
#define ESC "\033"
#define BRIGHT ESC "[1;37m"
#define GREEN ESC "[0;32m"
#define RED ESC "[0;31m"
#define RESET ESC "[0m"

// equivalent to basename(__FILE__)
#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

/* Test entry point. Should be paired with END_TESTS() in use */
#define BEGIN_TESTS(N)                                                                             \
    static Test_rs _##N##_test_main(Test_opts *opts);                                              \
    Test_rs (*N)(Test_opts * opts) = _##N##_test_main;                                             \
    static Test_rs _##N##_test_main(Test_opts *opts)                                               \
    {                                                                                              \
        Test_ctx __ctx;                                                                            \
        Test_ctx *_ctx = &__ctx;                                                                   \
        _test_init(_ctx, opts, #N);

#define END_TESTS()                                                                                \
    return (Test_rs){ __ctx.ran, __ctx.failed };                                                   \
    }

/* Define function to be used with TEST_CALL
 * Usage: TEST_FUNC(test_function_name) {
 *            // test code
 *        }
 */
#define TEST_FUNC(N) static void N(Test_ctx *_ctx)
#define TEST_CALL(S, F)                                                                            \
    _test_set_desc(_ctx, (S));                                                                     \
    F(_ctx);

#define GROUP(S) _test_set_group(_ctx, (S))

// GROUP_CALL is similar to TEST_CALL, and also calls functions defined
// with TEST_FUNC
#define GROUP_CALL(S, F)                                                                           \
    GROUP(S);                                                                                      \
    F(_ctx);

// If building on POSIX system, use fork to better isolate tests
// otherwise just print
#ifdef POSIX

#define __SETUP_TEST()                                                                             \
    int _line = __LINE__;                                                                          \
    pid_t _p = fork();                                                                             \
    if (_p == -1) {                                                                                \
        perror("fork failed");                                                                     \
        exit(-1);                                                                                  \
    } else if (_p == 0) {
#define __TEARDOWN_TEST()                                                                          \
    exit(!_res);                                                                                   \
    }                                                                                              \
    int _status;                                                                                   \
    if (waitpid(_p, &_status, 0) == -1 || !WIFEXITED(_status))                                     \
        _res = 0;                                                                                  \
    else                                                                                           \
        _res = !WEXITSTATUS(_status);                                                              \
    _test_assert(_ctx, __FILE__, _line, _res);                                                     \
    if (WIFSIGNALED(_status)) {                                                                    \
        int _sig = WTERMSIG(_status);                                                              \
        printf("%s (%d)\n", strsignal(_sig), _sig);                                                \
    }

#else

#define __SETUP_TEST()
#define __TEARDOWN_TEST() _test_assert(_ctx, __FILE__, __LINE__, _res);

#endif

#define EXE(...)                                                                                   \
    {                                                                                              \
        int _res = 0;                                                                              \
        __SETUP_TEST();                                                                            \
        do {                                                                                       \
            __VA_ARGS__                                                                            \
            _res = 1;                                                                              \
        } while (0);                                                                               \
        __TEARDOWN_TEST();                                                                         \
    }

#define TEST_DEF(S, ...)                                                                           \
    {                                                                                              \
        _test_set_desc(_ctx, (S));                                                                 \
        __VA_ARGS__                                                                                \
    }

/* Usage:
 *   TEST("description", {
 *     // test code
 *   });
 */
/*#define TEST(S, ...) { \
    TEST_DEF((S), { EXE(__VA_ARGS__); });\
}*/

#define TEST(S, N, ...)                                                                            \
    TEST_DEF((S), {                                                                                \
        MOCK_SKY_CTX(N);                                                                           \
        EXE(__VA_ARGS__);                                                                          \
        CLOSE_SKY_CTX(N);                                                                          \
    });

#define ASSERT(X)                                                                                  \
    fprintf(stderr, "Running ASSERT() in %s:%d\n", __FILENAME__, __LINE__);                        \
    if (!(X))                                                                                      \
        break;

#define EXTERN_TEST(N) extern Test_rs (*N)(Test_opts *)

#define RUN_TEST(N)                                                                                \
    {                                                                                              \
        EXTERN_TEST(N);                                                                            \
        Test_rs _rs = N(&opts);                                                                    \
        rs.ran += _rs.ran;                                                                         \
        rs.failed += _rs.failed;                                                                   \
    }

#define PRINT_RESULT(X) _test_print_rs((X))

// Mock utility macros

#define MOCK_SKY_LOG_CTX(N) Sky_ctx_t ctx = { .logf = _test_log }

#define MOCK_SKY_CTX(N) Sky_ctx_t *N = _test_sky_ctx()

#define CLOSE_SKY_CTX(C)                                                                           \
    Sky_errno_t err;                                                                               \
    if (sky_close(&err, NULL) != SKY_SUCCESS) {                                                    \
        fprintf(stderr, "error closing mock sky context\n");                                       \
        exit(-1);                                                                                  \
    }                                                                                              \
    free(C);

#define BEACON(N, TYPE, TIME, RSSI, CON)                                                           \
    Beacon_t N;                                                                                    \
    _test_beacon(&(N), (TYPE), (TIME), (RSSI), (CON));

#define AP(N, MAC, TIME, RSSI, FREQ, CON)                                                          \
    Beacon_t N;                                                                                    \
    _test_ap(&(N), (MAC), (TIME), (RSSI), (FREQ), (CON));

#define NR(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                       \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_NR, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define LTE(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                      \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_LTE, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define UMTS(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                     \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_UMTS, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define NBIOT(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                    \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_NBIOT, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define CDMA(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                     \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_CDMA, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define GSM(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                      \
    Beacon_t N;                                                                                    \
    _test_cell(                                                                                    \
        &(N), SKY_BEACON_GSM, (TIME), (RSSI), (CON), (ID1), (ID2), (ID3), (ID4), (ID5), (ID6));

#define NR_NMR(N, TIME, RSSI, CON, ID5, ID6)                                                       \
    Beacon_t N;                                                                                    \
    _test_cell(&(N), SKY_BEACON_NR, (TIME), (RSSI), (CON), -1, -1, -1, -1, (ID5), (ID6));

#define LTE_NMR(N, TIME, RSSI, CON, ID5, ID6)                                                      \
    Beacon_t N;                                                                                    \
    _test_cell(&(N), SKY_BEACON_LTE, (TIME), (RSSI), (CON), -1, -1, -1, -1, (ID5), (ID6));

#define UMTS_NMR(N, TIME, RSSI, CON, ID1, ID2, ID3, ID4, ID5, ID6)                                 \
    Beacon_t N;                                                                                    \
    _test_cell(&(N), SKY_BEACON_UMTS, (TIME), (RSSI), (CON), -1, -1, -1, -1, (ID5), (ID6));

#define NBIOT_NMR(N, TIME, RSSI, CON, ID5, ID6)                                                    \
    Beacon_t N;                                                                                    \
    _test_cell(&(N), SKY_BEACON_NBIOT, (TIME), (RSSI), (CON), -1, -1, -1, -1, (ID5), (ID6));

#define BEACON_EQ(A, B) _test_beacon_eq((A), (B))
#define AP_EQ(A, B) _test_ap_eq((A), (B))

typedef struct {
    int verbose;
} Test_opts;

typedef struct {
    const Test_opts *opts;
    const char *name;
    const char *file;
    const char *group;
    const char *desc;

    unsigned ran;
    unsigned failed;
} Test_ctx;

typedef struct {
    unsigned ran; // number of tests ran
    unsigned failed; // number of tests failed
} Test_rs;

void _test_init(Test_ctx *ctx, Test_opts *opts, const char *str);
void _test_set_group(Test_ctx *ctx, const char *str);
void _test_set_desc(Test_ctx *ctx, const char *str);
void _test_assert(Test_ctx *ctx, const char *file, int line, int res);
void _test_print_rs(Test_opts *opts, Test_rs rs);
int _test_log(Sky_log_level_t level, char *s);
Sky_ctx_t *_test_sky_ctx();
int _test_ap(Beacon_t *b, const char *mac, time_t timestamp, int16_t rssi, int32_t frequency,
    bool is_connected);
int _test_beacon(
    Beacon_t *b, Sky_beacon_type_t type, time_t timestamp, int16_t rssi, bool is_connected);
int _test_cell(Beacon_t *b, Sky_beacon_type_t type, time_t timestamp, int16_t rssi,
    bool is_connected, uint16_t id1, uint16_t id2, int32_t id3, int64_t id4, int16_t id5,
    int32_t freq);
bool _test_beacon_eq(const Beacon_t *a, const Beacon_t *b);
bool _test_ap_eq(const Beacon_t *a, const Beacon_t *b);
//int _test_cell(Beacon_t *b, time_t timestamp, int16_t rssi, int32_t frequency, bool is_connected);

#endif
