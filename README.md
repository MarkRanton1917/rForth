# rForth - Forth Interpreter for ESP32 and Linux

A lightweight, efficient **Forth interpreter** implementation designed for embedded systems, particularly optimized for **ESP32 microcontrollers** with full support for multitasking, floating-point arithmetic, and memory management.

> **Note:** rForth is a fork of [eForth v4.2](https://github.com/chochain/eForth), adapted and enhanced for modern embedded systems with cross-platform support.

## Features

### Core Forth Implementation
- **Stack-based execution model** - Traditional Forth word and number stack operations
- **Interactive interpreter** - Read-Eval-Print Loop (REPL) for real-time code execution
- **Word definitions** - Define custom words with `:` (colon) definitions
- **Control structures** - `if/then`, `begin/until`, `begin/while/repeat`, `do/loop`, `do/+loop`
- **Local variables** - `{: ... :}` named locals with `->` assignment, usable anywhere in a word body (including inside loops), with full recursion support; `f{: ... f:}` / `f->` provide the same for float locals when USE_FLOAT=1
- **Memory management** - Configurable heap size with dynamic memory allocation; dictionary words are `std::shared_ptr`-owned, so `forget`/`boot` correctly free everything they remove — including recursive words and bodies shared across multiple `CREATE...DOES>` instances
- **Case-sensitive or case-insensitive mode** - Compile-time configurable

### Advanced Features
- **Floating-point support** - Optional float type (DF) for mathematical operations
- **Cooperative multitasking** - Full support on FreeRTOS (ESP32) and pthreads (Linux)
- **Embedded scripting** - Easy integration as a component in larger projects
- **Cross-platform** - Runs on both ESP32 (with FreeRTOS) and Linux; porting to other platforms is straightforward through platform-specific implementations of `mem_stat()` and `forth_include()` functions with FreeRTOS abstraction layer support
- **gForth-compatible output format** - Output formatting and stack display follow gForth conventions for consistency with standard Forth systems

## Project Structure

```
rforth/
├── src/
│   ├── rForth.h          # Main header with core structures and macros
│   └── rForth.cpp        # Implementation of Forth interpreter
├── forth/
│   ├── memory.fs         # Forth utility library for memory operations
│   └── tests/
│       ├── task_test.fs      # Example task management in Forth
│       ├── stress_test.fs    # Regression test for dictionary/forget memory handling
│       └── numbers_test.fs   # Example number parsing (single and double)
├── examples/
│   ├── esp32-usart.cpp   # ESP32 UART interface example
│   └── linux.cpp         # Linux standalone example
├── CMakeLists.txt        # Build configuration for IDF component
├── LICENCE               # MIT License
└── README.md             # This file
```

## Building

### As an ESP-IDF Component

The project is designed as an ESP-IDF component. Include it in your project's `components/` directory and reference it in your `CMakeLists.txt`:

```cmake
idf_component_register(
  REQUIRES rforth
)
```

### Build Options (in `CMakeLists.txt`)

Configure the Forth interpreter with compile-time definitions:

```cmake
add_compile_definitions(
  CASE_SENSITIVE=1        # 1=case-sensitive, 0=case-insensitive
  PAD_SIZE=256            # Input buffer size
  HEAP_SIZE=65536         # Heap size in bytes
  USE_FLOAT=1             # 1=enable floating-point, 0=disable
)
```

### Linux Build

For Linux development and testing, compile with Linux platform definitions:

```bash
g++ -DLINUX_PLATFORM -DUSE_FLOAT=1 -DPAD_SIZE=256 -DHEAP_SIZE=65536 -DCASE_SENSITIVE=1 src/rForth.cpp examples/linux.cpp -Isrc -o rforth -lm
```

## Core Data Types

```cpp
typedef uint32_t U32;    // Unsigned 32-bit integer
typedef int32_t S32;     // Signed 32-bit integer
typedef int32_t DU;      // Data unit (stack word) - 32-bit on ESP32
typedef int64_t DU2;     // 64-bit double word
typedef float DF;        // Floating-point number (when USE_FLOAT=1)
typedef uintptr_t UFP;   // Function pointer type
```

## Usage Example

### Interactive Session

```forth
\ Define a simple word
: square ( n -- n² ) dup * ;

\ Test it
5 square .              \ Output: 25

\ Memory operations
100 5 dump              \ Dump 5 cells from address 100

\ Task creation (with threading support)
variable tid
: worker ." Starting task" cr ;
' worker task tid !

\ Check if task is active
tid @ active? if ." Task is running" cr then

\ Pause to yield control to other tasks
pause
```

## Local Variables

rForth supports named local variables declared with `{: ... :}`. Once declared, a local is read simply by naming it, and written with `->`.

### Declaring locals

```forth
: SUM3  {: a b c -- total :}
  a b c + +  -> total
  total ;

1 2 3 SUM3 .          \ 6
```

- Names listed **before** `--` are popped off the data stack, left to right (the rightmost name gets the top of stack).
- Names listed **after** `--` are plain local variables, initialized to `0`. If `--` is omitted entirely, every name is instead popped off the data stack (same as being listed before `--`).
- `{: ... :}` — including its name list — must fit on a single source line, the same restriction as `."`, `s"` and `abort"`.
- Writing to a local is always explicit, via `-> name`; reading it is just using its name like any other word.

### Where `{:` can appear

Unlike many other Forth locals implementations, `{: ... :}` is **not** restricted to the very start of a word. It can appear anywhere in the body — after other code, inside `if`/`else`, inside `begin`/`until`, and even inside `do...loop` (each loop iteration reuses the same slot instead of leaking a new one on every pass):

```forth
: SUM-OF-SQUARES  {: n :}
  0
  n 0 do
    {: -- sq :}        \ (re)declared fresh on every iteration
    i i * -> sq
    sq +
  loop ;

5 SUM-OF-SQUARES .      \ 30  (0+1+4+9+16)
```

Additional `{: ... :}` blocks later in the same word simply add more locals to the same frame:

```forth
: DIST  {: x1 y1 :}
  {: x2 y2 :}
  x2 x1 - dup *
  y2 y1 - dup * + ;

0 0 3 4 DIST .           \ 25
```

Recursion is fully supported — every recursive call gets its own independent set of locals, isolated from every other call on the stack:

```forth
: FACT  {: n :}
  n 1 <= if
    1
  else
    n 1 - {: sub :}
    sub FACT n * -> sub
    sub
  then ;

5 FACT .                 \ 120
```

### Limitations

- The name list of a `{: ... :}` block must be entirely on one source line.
- A local declared inside only *one* branch of `if ... else ... then`, and then read after the branches merge (past `then`), is not supported — declare such locals either unconditionally (before the `if`) or symmetrically, with the same names in the same order, in both branches.

### Float locals

When USE_FLOAT=1, `f{: ... f:}` and `f-> name` give the float stack the same locals mechanism, kept in its own frame alongside the integer one:

```forth
: CIRCLE-AREA  f{: r -- area f:}
  r r f* 3.14159 f* f-> area
  area ;

3.0 CIRCLE-AREA f.        \ 28.2743
```

Everything above — placement anywhere in a word, multiple blocks adding to the same frame, per-call isolation under recursion — applies identically to `f{: ... f:}`. Integer and float locals are tracked independently, so a word can freely mix `{: ... :}` and `f{: ... f:}`, and the same name may be reused for both an integer and a float local without conflict.

## Complete Word Reference

All built-in words available in rForth, organized by category:

### Arithmetic Operations
- `+ ( n1 n2 -- n )` - Add two numbers
- `- ( n1 n2 -- n )` - Subtract: n1 - n2
- `* ( n1 n2 -- n )` - Multiply two numbers
- `/ ( n1 n2 -- n )` - Divide: n1 / n2
- `mod ( n1 n2 -- n )` - Modulo: n1 mod n2
- `*/ ( n1 n2 n3 -- n )` - Multiply and divide: (n1 * n2) / n3
- `/mod ( n1 n2 -- rem quot )` - Both remainder and quotient
- `*/mod ( n1 n2 n3 -- rem quot )` - (n1 * n2) mod n3, then quot

### Double-Number Operations

Double numbers are a `lo hi` cell pair (same convention as `d>f`/`f>d`), letting integers wider than a single cell round-trip through the stack. `number`/`number?` produce a `lo hi` pair for a classic Forth double literal (digits with a trailing `.`, e.g. `123456789012.`) that overflows a single cell.

- `s>d ( n -- lo hi )` - Sign-extend a single number to double
- `d>s ( lo hi -- n )` - Truncate a double to a single number (drops the high cell)
- `d+ ( lo1 hi1 lo2 hi2 -- lo3 hi3 )` - Double addition
- `d- ( lo1 hi1 lo2 hi2 -- lo3 hi3 )` - Double subtraction
- `dnegate ( lo hi -- lo' hi' )` - Negate the combined 64-bit double value
- `dabs ( lo hi -- lo' hi' )` - Absolute value of the combined 64-bit double value
- `dmax ( d1 d2 -- d )` - Maximum of two doubles
- `dmin ( d1 d2 -- d )` - Minimum of two doubles
- `d= ( d1 d2 -- flag )` - Equality
- `d<> ( d1 d2 -- flag )` - Not equal
- `d< ( d1 d2 -- flag )` - Less than (signed)
- `d> ( d1 d2 -- flag )` - Greater than (signed)
- `d<= ( d1 d2 -- flag )` - Less than or equal (signed)
- `d>= ( d1 d2 -- flag )` - Greater than or equal (signed)
- `d0= ( lo hi -- flag )` - Zero equal
- `d0<> ( lo hi -- flag )` - Zero not equal
- `d0< ( lo hi -- flag )` - Zero less (negative)
- `d0> ( lo hi -- flag )` - Zero greater (positive)
- `d0<= ( lo hi -- flag )` - Zero less or equal
- `d0>= ( lo hi -- flag )` - Zero greater or equal
- `d. ( lo hi -- )` - Print a double number in the current base

### Bitwise Operations
- `and ( u1 u2 -- u )` - Bitwise AND
- `or ( u1 u2 -- u )` - Bitwise OR
- `xor ( u1 u2 -- u )` - Bitwise XOR
- `invert ( u -- u )` - Bitwise NOT (invert all bits)
- `lshift ( u n -- u )` - Left shift by n bits
- `rshift ( u n -- u )` - Right shift by n bits

### Unary Operations
- `abs ( n -- |n| )` - Absolute value
- `negate ( n -- -n )` - Negate (multiply by -1)
- `1+ ( n -- n+1 )` - Add 1
- `1- ( n -- n-1 )` - Subtract 1
- `2* ( n -- n*2 )` - Multiply by 2
- `2/ ( n -- n/2 )` - Divide by 2
- `max ( n1 n2 -- max )` - Maximum of two numbers
- `min ( n1 n2 -- min )` - Minimum of two numbers

### Comparison Operations
- `= ( n1 n2 -- flag )` - Equal (returns -1 for true, 0 for false)
- `<> ( n1 n2 -- flag )` - Not equal
- `< ( n1 n2 -- flag )` - Less than: n1 < n2
- `> ( n1 n2 -- flag )` - Greater than: n1 > n2
- `<= ( n1 n2 -- flag )` - Less than or equal
- `>= ( n1 n2 -- flag )` - Greater than or equal
- `u< ( u1 u2 -- flag )` - Unsigned less than
- `u> ( u1 u2 -- flag )` - Unsigned greater than
- `u<= ( u1 u2 -- flag )` - Unsigned less than or equal
- `u>= ( u1 u2 -- flag )` - Unsigned greater than or equal
- `u= ( u1 u2 -- flag )` - Unsigned equal
- `u<> ( u1 u2 -- flag )` - Unsigned not equal
- `0= ( n -- flag )` - Zero equal (n == 0)
- `0< ( n -- flag )` - Zero less (n < 0)
- `0> ( n -- flag )` - Zero greater (n > 0)
- `0<> ( n -- flag )` - Zero not equal (n != 0)
- `0<= ( n -- flag )` - Zero less or equal (n <= 0)
- `0>= ( n -- flag )` - Zero greater or equal (n >= 0)

### Stack Manipulation
- `dup ( x -- x x )` - Duplicate top of stack
- `drop ( x -- )` - Remove top of stack
- `swap ( x1 x2 -- x2 x1 )` - Swap two top items
- `over ( x1 x2 -- x1 x2 x1 )` - Copy second item over top
- `rot ( x1 x2 x3 -- x2 x3 x1 )` - Rotate top three
- `-rot ( x1 x2 x3 -- x3 x1 x2 )` - Rotate back top three
- `nip ( x1 x2 -- x2 )` - Remove second item
- `tuck ( x1 x2 -- x2 x1 x2 )` - Insert top below second
- `?dup ( x -- x | x x )` - Duplicate if not zero
- `pick ( ... u -- ... x )` - Copy u-th item to top
- `2dup ( x1 x2 -- x1 x2 x1 x2 )` - Duplicate top two
- `2drop ( x1 x2 -- )` - Remove top two
- `2swap ( x1 x2 x3 x4 -- x3 x4 x1 x2 )` - Swap two pairs
- `2over ( x1 x2 x3 x4 -- x1 x2 x3 x4 x1 x2 )` - Copy first pair over second
- `depth ( -- n )` - Get stack depth

### Return Stack Operations
- `>r ( x -- )` (return stack: -- x) - Move to return stack
- `r> ( -- x )` (return stack: x -- ) - Pop from return stack
- `r@ ( -- x )` (return stack: x -- x) - Copy from return stack (peek)

### Input/Output
- `emit ( c -- )` - Output a character (ASCII code)
- `key ( -- c )` - Read a character (blocking)
- `key? ( -- flag )` - Check if a character is available (non-blocking)
- `accept ( c-addr +n1 -- +n2 )` - Read up to +n1 characters into buffer with backspace editing, returns actual count
- `number ( c-addr len -- n | lo hi )` - Parse a string as a number: a plain integer that fits in a cell; a float when USE_FLOAT=1 (pushed to the float stack instead); or, classic Forth double-number syntax — digits with a trailing `.` and nothing after it (e.g. `123456789012.`) that overflow a single cell — pushed as a `lo hi` pair (the same convention `d>f`/`f>d` use). Throws if the string isn't a valid number.
- `number? ( c-addr len -- n true | lo hi true | c-addr len false )` - Like `number`, but returns a false flag instead of throwing on an invalid string; on failure, the original `c-addr len` are left on the stack below the flag
- `cr ( -- )` - Output carriage return and line feed
- `space ( -- )` - Output one space
- `spaces ( n -- )` - Output n spaces
- `type ( addr len -- )` - Output string from address with length
- `. ( n -- )` - Print number in current base
- `.r ( n width -- )` - Print number right-aligned in width
- `u.r ( u width -- )` - Print unsigned number right-aligned
- `.s ( -- )` - Print stack contents
- `?  ( addr -- )` - Print cell at address
- `bl ( -- 0x20 )` - Return space character code
- `ascii ( "c" -- n )` - Parse the next word as a single character and push its ASCII code

### String and Comment Operations (compile-time)
- `( ... ) ` - Multi-line comment (immediate)
- `\ ... ` - Single-line comment to end of line (immediate)
- `.( string ) ` - Print string immediately (immediate)
- `." string " ` - Print string at compile/execution time (immediate)
- `s" string " ( -- addr len )` - Create string literal, returns address and length (immediate). Interpreted outside a colon definition, the buffer is transient (backed by the task's PAD, per ANS Forth convention) and only valid until the next PAD-consuming operation; compiled inside a word, its text is owned by the word and lives as long as the word does.
- `abort" string " ` - Abort with message string (immediate)
- `abort ( -- )` - Abort with the default "Aborted" message

### Number Output Formatting
- `<# ( -- )` - Begin number formatting
- `# ( n -- n/base )` - Format one digit
- `#s ( n -- 0 )` - Format remaining digits
- `#> ( n -- addr len )` - End formatting, return address and length
- `hold ( c -- )` - Add character to formatted number

### Memory and Variables
- `@ ( addr -- val )` - Fetch cell from address
- `! ( val addr -- )` - Store cell to address
- `c@ ( addr -- byte )` - Fetch byte (8-bit) from address
- `c! ( byte addr -- )` - Store byte (8-bit) to address
- `+! ( val addr -- )` - Add to value at address
- `, ( n -- )` - Allocate and store cell in heap
- `cells ( n -- bytes )` - Convert cell count to byte count
- `allot ( n -- )` - Allocate n bytes in heap
- `here ( -- addr )` - Get current heap pointer
- `pad ( -- addr )` - Address of the current task's scratch buffer (same buffer `<#`/`hold`/`#>` and interpreted `s"` use)
- `variable ( "name" -- )` - Create a variable (allocates one cell)
- `constant ( n "name" -- )` - Create a constant
- `fvariable ( "name" -- )` - Create floating-point variable (when USE_FLOAT=1)
- `fconstant ( f "name" -- )` - Create floating-point constant (when USE_FLOAT=1)

### Word Definition and Lookup
- `: name ... ;` - Define a new word (compile mode)
- `[ ( -- )` - Switch to interpretation mode
- `] ( -- )` - Switch to compilation mode
- `immediate ( -- )` - Make last word immediate
- `execute ( xt -- )` - Execute word at execution token
- `' name ( -- xt )` - Get execution token of word
- `['] name ( -- xt )` - Get execution token at compile time
- `create ( "name" -- )` - Create a named data structure
- `does> ( -- )` - Define behavior of created words
- `see ( "name" -- )` - Disassemble and display word definition
- `words ( -- )` - List all defined words
- `forget ( "name" -- )` - Delete word and all words after it
- `boot ( -- )` - Reset to initial state (delete user words)

### Base and Number Parsing
- `base ( -- addr )` - Address of current base variable
- `decimal ( -- )` - Set base to 10
- `hex ( -- )` - Set base to 16

### Control Structures (compile-only)
- `if ... then` - Conditional execution
- `if ... else ... then` - Conditional with alternative
- `begin ... until` - Loop until condition true
- `begin ... while ... repeat` - While loop
- `begin ... again` - Infinite loop
- `do ... loop` - Fixed count loop (i = index)
- `do ... +loop` - Fixed count loop with variable step
- `i ( -- n )` - Get current loop index
- `leave ( -- )` - Exit loop immediately
- `exit ( -- )` - Exit from current word

### Local Variables (compile-only)
- `{: name1 name2 ... -- name3 name4 ... :}` - Declare local variables. Names before `--` are popped off the data stack (rightmost name gets top of stack); names after `--` are plain locals initialized to `0`. If `--` is omitted entirely, every name is popped off the data stack instead. See [Local Variables](#local-variables) below for details.
- `-> name` - Pop the top of the data stack into the named local

### Timing and System
- `ms ( -- ms )` - Get milliseconds since boot
- `delay ( ms -- )` - Sleep for milliseconds
- `pause ( -- )` - Yield control to scheduler
- `rnd ( -- n )` - Get random number
- `mstat ( -- )` - Print memory statistics
- `bye ( -- )` - Exit Forth system

### Task Management
- `task ( xt -- id )` - Create new task from execution token
- `active? ( id -- flag )` - Check if task is active
- `resume ( id -- )` - Resume suspended task
- `stop ( -- )` - Stop current task

### Floating-Point Operations (when USE_FLOAT=1)

#### Arithmetic
- `f+ ( f1 f2 -- f )` - Float addition
- `f- ( f1 f2 -- f )` - Float subtraction
- `f* ( f1 f2 -- f )` - Float multiplication
- `f/ ( f1 f2 -- f )` - Float division
- `f** ( f1 f2 -- f )` - Float exponentiation (power)
- `f2* ( f -- f )` - Multiply by 2.0
- `f2/ ( f -- f )` - Divide by 2.0
- `1/f ( f -- f )` - Reciprocal (1/f)

#### Unary Functions
- `fsqrt ( f -- f )` - Square root
- `fnegate ( f -- f )` - Negate float
- `fabs ( f -- f )` - Absolute value

#### Comparison
- `fmin ( f1 f2 -- f )` - Minimum of two floats
- `fmax ( f1 f2 -- f )` - Maximum of two floats
- `f= ( f1 f2 -- flag )` - Equal
- `f<> ( f1 f2 -- flag )` - Not equal
- `f< ( f1 f2 -- flag )` - Less than
- `f> ( f1 f2 -- flag )` - Greater than
- `f<= ( f1 f2 -- flag )` - Less than or equal
- `f>= ( f1 f2 -- flag )` - Greater than or equal
- `f0= ( f -- flag )` - Zero equal (f == 0.0)
- `f0< ( f -- flag )` - Zero less (f < 0.0)
- `f0> ( f -- flag )` - Zero greater (f > 0.0)
- `f0<> ( f -- flag )` - Zero not equal (f != 0.0)
- `f0<= ( f -- flag )` - Zero less or equal (f <= 0.0)
- `f0>= ( f -- flag )` - Zero greater or equal (f >= 0.0)
- `f~ ( f1 f2 f3 -- flag )` - Approximate equality per the ANS Forth standard; the mode is chosen by the sign of `f3`: positive is an absolute tolerance (`|f1-f2| < f3`), zero requires exact equality, negative is a relative tolerance (`|f1-f2| < |f3| * (|f1|+|f2|)`)

#### Trigonometric
- `fsin ( f -- f )` - Sine (radians)
- `fcos ( f -- f )` - Cosine (radians)
- `ftan ( f -- f )` - Tangent (radians)
- `fasin ( f -- f )` - Arcsine
- `facos ( f -- f )` - Arccosine
- `fatan ( f -- f )` - Arctangent
- `fatan2 ( f1 f2 -- f )` - Two-argument arctangent

#### Hyperbolic
- `fsinh ( f -- f )` - Hyperbolic sine
- `fcosh ( f -- f )` - Hyperbolic cosine
- `ftanh ( f -- f )` - Hyperbolic tangent
- `fasinh ( f -- f )` - Inverse hyperbolic sine
- `facosh ( f -- f )` - Inverse hyperbolic cosine
- `fatanh ( f -- f )` - Inverse hyperbolic tangent

#### Exponential and Logarithmic
- `fexp ( f -- f )` - Exponential (e^f)
- `fln ( f -- f )` - Natural logarithm
- `flog ( f -- f )` - Base-10 logarithm
- `falog ( f -- f )` - 10^f (antilog base 10)
- `pi ( -- f )` - Push π (3.14159...)

#### Rounding
- `floor ( f -- f )` - Round down to integer
- `fround ( f -- f )` - Round to nearest integer

#### Float Stack Manipulation
- `fdup ( f -- f f )` - Duplicate top of float stack
- `fdrop ( f -- )` - Remove top of float stack
- `fswap ( f1 f2 -- f2 f1 )` - Swap two floats
- `fover ( f1 f2 -- f1 f2 f1 )` - Copy second over top
- `frot ( f1 f2 f3 -- f2 f3 f1 )` - Rotate top three
- `fnip ( f1 f2 -- f2 )` - Remove second float
- `ftuck ( f1 f2 -- f2 f1 f2 )` - Insert top below second
- `fpick ( ... u -- ... f )` - Copy u-th float to top

#### Float Memory Access
- `f@ ( addr -- f )` - Fetch float from address
- `f! ( f addr -- )` - Store float to address
- `floats ( n -- bytes )` - Convert float count to bytes

#### Float Type Conversion
- `s>f ( n -- f )` - Convert signed integer to float
- `f>s ( f -- n )` - Convert float to signed integer
- `d>f ( lo hi -- f )` - Convert double to float
- `f>d ( f -- lo hi )` - Convert float to double

#### Float Output
- `f. ( f -- )` - Print float
- `f.r ( f prec width -- )` - Print float with precision and width
- `f.s ( -- )` - Print float stack contents

#### Float Local Variables (compile-only)
- `f{: name1 name2 ... -- name3 name4 ... f:}` - Declare float local variables. Works exactly like `{: ... :}` (see [Local Variables](#local-variables)) but pulls its inputs from the float stack instead of the data stack, and its own name list is delimited by `f{:` / `f:}`. Float and integer locals coexist freely in the same word and are looked up independently, so the same name can be used for both an integer and a float local without conflict.
- `f-> name` - Pop the top of the float stack into the named float local

### File Operations
- `included ( addr len -- )` - Load and execute Forth file

## Standard Library (Forth)

### Memory Utilities (forth/memory.fs)

- `dump ( n-cells addr -- )` - Dump n cells starting from address
- `cdump ( n-bytes addr -- )` - Dump n bytes starting from address
- `memset ( val n-cells addr -- )` - Fill n cells with value
- `cmemset ( val n-bytes addr -- )` - Fill n bytes with value

## Macros for Defining Words

```cpp
// Define a normal word
CODE("WORD_NAME", { 
  // implementation 
})

// Define an immediate word (executes during compilation)
IMMD("IMMD_NAME", { 
  // implementation 
})

// Define a compile-only word
COMP("COMP_NAME", { 
  // implementation 
})

// Define an immediate, compile-only word (e.g. if/else/do/loop)
ICOMP("ICOMP_NAME", { 
  // implementation 
})
```

## Architecture

### Stack-Based Computation

rForth uses the traditional Forth stack model:
- **Data Stack**: Operands and results
- **Return Stack**: Function return addresses and loop parameters
- **Dictionary**: Compiled word definitions

### Execution Model

1. **Parsing** - Input is read word-by-word
2. **Lookup** - Each word is searched in the dictionary
3. **Execution** - Found words execute immediately or compile depending on state
4. **Compilation** - Colon definitions build new words from existing ones

### Memory Layout

- **Heap** - Dynamic allocations for word definitions
- **PAD** - Input buffer for interactive commands
- **Stacks** - Return and data stacks in task-local storage

## Multitasking Support

rForth provides built-in words for task management:

- **`task ( xt -- id )`** - Create a new task from execution token (xt), returns task ID
- **`active? ( id -- flag )`** - Check if task with given ID is active (true/-1 or false/0)
- **`pause ( -- )`** - Yield control to other tasks
- **`resume ( id -- )`** - Resume a suspended task
- **`stop ( -- )`** - Suspend/terminate the current task

### Multitasking Example

```forth
variable tid1
variable tid2

: task1 10 0 do ." Task 1: " i . cr 100 delay loop ;
: task2 10 0 do ." Task 2: " i . cr 150 delay loop ;

: main
  ' task1 task tid1 !
  ' task2 task tid2 !
  begin
    pause
    tid1 @ active? 0= tid2 @ active? 0= and
  until
  ." All tasks completed" cr
;

main
```

## Platform Abstraction

The code uses conditional compilation to support multiple platforms:

```cpp
#if ESP_PLATFORM
  // ESP32-specific code (FreeRTOS)
#elif LINUX_PLATFORM
  // Linux-specific code (pthreads)
#endif
```

## Example Programs

### esp32-usart.cpp

Provides UART interface for interactive Forth console on ESP32.

### linux.cpp

Standalone Forth interpreter for Linux systems.

### forth/tests/task_test.fs

Demonstrates task creation and management in Forth.

### forth/tests/stress_test.fs

Exercises `forget`/`boot` against `CREATE...DOES>` instances, repeated word redefinitions, recursion, and control structures — useful for checking under a memory checker (e.g. valgrind) that dictionary memory is fully reclaimed.

### forth/tests/numbers_test.fs

Reads a number from input and doubles it, exercising `accept`, `number?`, both single- and double-number (`lo hi`) results, and the invalid-input case where `number?` leaves `c-addr len false` on the stack.

## System Integration

### ESP32-specific Functions

- **Task management**: `xTaskCreate`, `vTaskDelete`, task suspend/resume
- **Timing**: `esp_timer_get_time()` for millisecond precision
- **Synchronization**: FreeRTOS recursive mutexes and semaphores

### Linux-specific Functions

- **Threading**: `std::thread` for task management
- **Synchronization**: `std::recursive_mutex`
- **Timing**: `std::chrono` for time operations

## License

This project is licensed under the **MIT License**. See the [LICENCE](LICENCE) file for details.

## Author

Vladimir Egorov (2026)

## Contributing

Contributions are welcome! Please ensure code follows the existing style and includes appropriate documentation.

---

**rForth** brings the elegant simplicity and power of the Forth programming language to resource-constrained embedded systems while maintaining full compatibility with modern multitasking environments.
