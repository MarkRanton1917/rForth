///
/// eForth - Configuration and Cross Platform macros
///
#pragma once
///
/// Benchmark: 10K*10K cycles on desktop (3.2G AMD)
///    RANGE_CHECK     0 cut 100ms
///    INLINE            cut 545ms
///
///@name Conditional compililation options
///@}
#define CC_DEBUG 1 /**< debug level 0|1|2      */
#define CASE_SENSITIVE 1 /**< word case sensitive    */
#define PAD_LENGTH 64
#define DO_WASM __EMSCRIPTEN__ /**< for WASM output        */
///@}
///
///@name Logical units (instead of physical) for type check and portability
///@{
typedef uint32_t U32; ///< unsigned 32-bit integer
typedef int32_t S32; ///< signed 32-bit integer
typedef uint16_t U16; ///< unsigned 16-bit integer
typedef uint8_t U8; ///< byte, unsigned character
typedef uintptr_t UFP; ///< function pointer as integer
typedef uint16_t IU; ///< instruction pointer unit

typedef int64_t DU2;
typedef int32_t DU;
#define DU0 0
#define DU1 1
#define DU_EPS 0
#define UINT(v) (static_cast<U32>(v))
#define MOD(m, n) ((m) % (n))
#define ABS(v) (abs(v))
#define ZEQ(v) ((v) == DU0)
#define EQ(a, b) ((a) == (b))
#define LT(a, b) ((a) < (b))
#define GT(a, b) ((a) > (b))
#define RND() (rand())

///@}
///@name String comparison
///@{
#if CASE_SENSITIVE
#define STRCMP(a, b) (strcmp(a, b))
#else // !CASE_SENSITIVE
#include <strings.h> // strcasecmp
#define STRCMP(a, b) (strcasecmp(a, b))
#endif // CASE_SENSITIVE
///@}
///@name Multi-platform support
///@{
#define ENDL "\r\n"

#if (ARDUINO || ESP32)
#include <Arduino.h>
#define to_string(i) string(String(i).c_str())
#if ESP32
#define analogWrite(c, v, mx) ledcWrite((c), (8191 / mx) * min((int)(v), mx))
#endif // ESP32

#elif DO_WASM
#include <emscripten.h>
#define millis() EM_ASM_INT({ return Date.now(); })
#define delay(ms)                                                              \
    EM_ASM({ let t = setTimeout(() = > clearTimeout(t), $0); }, ms)
#define yield() /* JS is async */

#else // !(ARDUINO || ESP32) && !DO_WASM
#include <chrono>
#include <thread>
#define millis()                                                               \
    chrono::duration_cast<chrono::milliseconds>(                               \
        chrono::steady_clock::now().time_since_epoch())                        \
        .count()
#define delay(ms) this_thread::sleep_for(chrono::milliseconds(ms))
#define yield() this_thread::yield()
#define PROGMEM

#endif // (ARDUINO || ESP32)
///@}
///@name Logging support
///@{
#if (ARDUINO || ESP32)
#define LOGS(s) Serial.print(F(s))
#define LOG(v) Serial.print(v)
#define LOGX(v) Serial.print(v, HEX)
#else // !(ARDUINO || ESP32)
#define LOGS(s) printf("%s", s)
#define LOG(v) printf("%-ld", (int64_t)(v))
#define LOGX(v) printf("%-lx", (uint64_t)(v))
#endif // (ARDUINO || ESP32)

#define LOG_NA() LOGS("N/A\n")
#define LOG_KV(k, v)                                                           \
    LOGS(k);                                                                   \
    LOG(v)
#define LOG_KX(k, x)                                                           \
    LOGS(k);                                                                   \
    LOGX(x)
#define LOG_HDR(f, s)                                                          \
    LOGS(f);                                                                   \
    LOGS("(");                                                                 \
    LOGS(s);                                                                   \
    LOGS(") => ")
#define LOG_DIC(i)                                                             \
    LOGS("dict[");                                                             \
    LOG(i);                                                                    \
    LOGS("] ");                                                                \
    LOGS(dict[i].name);                                                        \
    LOGS(" attr=");                                                            \
    LOGX(dict[i].attr);                                                        \
    LOGS("\n")
///@}
