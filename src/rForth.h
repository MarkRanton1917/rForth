// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#define DU0 0
#define DU1 1
#define UINT(v) (static_cast<U32>(v))
#define MOD(m, n) ((m) % (n))
#define ABS(v) (abs(v))
#define ZEQ(v) ((v) == DU0)
#define EQ(a, b) ((a) == (b))
#define LT(a, b) ((a) < (b))
#define GT(a, b) ((a) > (b))
#define RND() (rand())
#define ENDL "\n"
#define WORD_MARKER_FRAME ((DU)0xDEADBEEF)
#define LOCALS_MARKER_FRAME ((DU)0xFEEDC0DE)
#define INPUT_NONE -1
#define INPUT_BREAK 3
#define INPUT_ENDL ENDL

#if CASE_SENSITIVE
#define STRCMP(a, b) (strcmp(a, b))
#else
#include <strings.h>
#define STRCMP(a, b) (strcasecmp(a, b))
#endif

#define CODE(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ }
#define IMMD(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG }
#define COMP(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::COMPILE_ONLY_FLAG }
#define ICOMP(s, g) { s, #g, [](Code *c) { g; }, __COUNTER__ | Code::IMMD_FLAG | Code::COMPILE_ONLY_FLAG }
#define BOOL(f) ((f) ? -1 : 0)

typedef uint32_t U32;
typedef int32_t S32;
typedef uint16_t U16;
typedef uint8_t U8;
typedef uintptr_t UFP;
typedef int64_t DU2;
#if ESP_PLATFORM
typedef int32_t DU;
#elif LINUX_PLATFORM
typedef int64_t DU;
#endif
#if USE_FLOAT
typedef float DF;
#define LOCALS_MARKER_FRAME_F ((DF)-1.0e30f)
#endif

template<typename T>
struct FV : public std::vector<T> {
  FV* merge(FV<T>& v);
  void push(T n);
  T pop();
  T& operator[](int i);
  const T& operator[](int i) const;
};

struct Code;
typedef void (*XT)(Code*);

enum class CodeType {
  WORD,
  COMMENT,
  TMP,
  LIT,
  FLIT,
  VAR,
  STR,
  BRAN
};

struct Code : std::enable_shared_from_this<Code> {
  const static U32 IMMD_FLAG = 0x80000000;
  const static U32 COMPILE_ONLY_FLAG = 0x40000000;
  const char* name;
  const char* desc = "";
  XT xt;
  FV<std::shared_ptr<Code>> pf;
  FV<std::shared_ptr<Code>> p1;
  FV<std::shared_ptr<Code>> p2;
  FV<DU> q;
  union {
    U32 attr;
    struct {
      U32 token        :28;
      U32 stage        :2;
      U32 compile_only :1;
      U32 immd         :1;
    };
  };
  CodeType code_type = CodeType::WORD;
  size_t heap_mark = 0;
  std::unique_ptr<char[]> name_buf;
  std::unique_ptr<char[]> desc_buf;
  Code(const char* s, const char* d, XT fp, U32 a);
  Code(const std::string s, bool n = true);
  Code(XT fp);
  Code* append(Code* w);
  Code* append(std::shared_ptr<Code> w);
  void exec();
  void set_name(const std::string& s);
  void set_desc(const std::string& s);
};

struct Comment : Code {
  Comment(const std::string& text, bool dot);
};

struct Tmp : Code {
  Tmp();
};

struct Lit : Code {
  Lit(DU d);
};

#if USE_FLOAT
struct FLit : Code {
  DF val;
  FLit(DF v);
};
#endif

struct Var : Code {
  Var(DU d);
};

struct Str : Code {
  Str(std::string s, int tok = 0, int len = 0, bool print = false);
};

struct Bran : Code {
  Bran(XT fp);
};

struct ForthContext {
  FV<DU> ss;
  FV<DU> rs;
  FV<DU> ls;
#if USE_FLOAT
  FV<DF> fs;
  FV<DF> lfs;
#endif
  const FV<std::shared_ptr<Code>>* pf;
  size_t ip;
  FV<Code*> call_stack;
  Code* xt;
  bool finished;
  void* handle;
  bool active;
  char pad[PAD_SIZE];
  size_t pad_ptr;
  int key_peek = INPUT_NONE;
};

struct Local {
  std::string name;
  int slot;
};

// api
void forth_init();
int forth_interpret(std::string input, void (*output_hook)(int, const char*));
int forth_vm(int (*input_hook)(), void (*output_hook)(int, const char*));
bool forth_waiting_input();
void forth_request_interrupt();
void forth_dict_add(const Code* words, size_t size);

void ss_push(DU n);
DU ss_pop();
#if USE_FLOAT
void fs_push(DF n);
DF fs_pop();
#endif
DU alloc_heap(const uint8_t* val, size_t size);

enum ForthFileMode {
  FAM_RO = 0,
  FAM_WO = 1,
  FAM_RW = 2
};

// implement and derive your file operations from this class
class ForthFile {
public:
  virtual ~ForthFile() = default;
  virtual void close() = 0;
  virtual long read(char* buf, long len) = 0; // bytes read; 0 = EOF; -1 = error
  virtual long write(const char* buf, long len) = 0; // bytes written; -1 = error
  virtual long read_line(char* buf, long max_len) = 0; // bytes read (excl. '\n'); -1 = EOF
  virtual bool seek(long pos) = 0;
  virtual long position() = 0; // -1 = error
  virtual long size() = 0; // -1 = error
};

// to implement — factory: open path with the host's native API and return a
// heap-allocated ForthFile subclass, or nullptr on failure.
ForthFile* forth_file_open(const char* path, int fam, bool create);
bool forth_file_delete(const char* path);
