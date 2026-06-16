#include "ceforth.h"
#include "mcu.h"

#include <sstream>
#include <cstring>

using namespace std;

FV<Code*> dict;
FV<DU> ss;
FV<DU> rs;
vector<uint8_t> heap;
size_t heap_ptr = 0;
bool compile = false;
Code* last;

istringstream fin;
ostringstream fout;
char pad[PAD_SIZE];
size_t pad_ptr;
void (*fout_cb)(int, const char*);

const Code rom[] = {
  CODE("bye", exit(0)),
  CODE("+",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a + b);
    }),
  CODE("-",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a - b);
    }),
  CODE("*",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a * b);
    }),
  CODE("/",
    {
      DU b = POP();
      DU a = POP();
      PUSH(a / b);
    }),
  CODE("mod",
    {
      DU b = POP();
      DU a = POP();
      PUSH(MOD(a, b));
    }),
  CODE("*/",
    {
      DU b = POP();
      DU a = POP();
      DU c = POP();
      PUSH(a * b / c);
    }),
  CODE("/mod",
    {
      DU b = POP();
      DU a = POP();
      DU m = MOD(a, b);
      PUSH(m);
      PUSH(a / b);
    }),
  CODE("*/mod",
    {
      DU b = POP();
      DU a = POP();
      DU c = POP();
      DU2 n = (DU2)a * b;
      DU2 m = MOD(n, c);
      PUSH((DU)m);
      PUSH((DU)(n / c));
    }),
  CODE("and",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) & UINT(b));
    }),
  CODE("or",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) | UINT(b));
    }),
  CODE("xor",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) ^ UINT(b));
    }),
  CODE("abs",
    {
      DU a = POP();
      PUSH(ABS(a));
    }),
  CODE("negate",
    {
      DU a = POP();
      PUSH(-a);
    }),
  CODE("invert",
    {
      DU a = POP();
      PUSH(~UINT(a));
    }),
  CODE("rshift",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) >> UINT(b));
    }),
  CODE("lshift",
    {
      DU b = POP();
      DU a = POP();
      PUSH(UINT(a) << UINT(b));
    }),
  CODE("max",
    {
      DU b = POP();
      DU a = POP();
      PUSH((a > b) ? a : b);
    }),
  CODE("min",
    {
      DU b = POP();
      DU a = POP();
      PUSH((a < b) ? a : b);
    }),
  CODE("2*",
    {
      DU a = POP();
      PUSH(a * 2);
    }),
  CODE("2/",
    {
      DU a = POP();
      PUSH(a / 2);
    }),
  CODE("1+",
    {
      DU a = POP();
      PUSH(a + 1);
    }),
  CODE("1-",
    {
      DU a = POP();
      PUSH(a - 1);
    }),
  CODE("0=",
    {
      DU a = POP();
      PUSH(BOOL(ZEQ(a)));
    }),
  CODE("0<",
    {
      DU a = POP();
      PUSH(BOOL(LT(a, DU0)));
    }),
  CODE("0>",
    {
      DU a = POP();
      PUSH(BOOL(GT(a, DU0)));
    }),
  CODE("=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(EQ(a, b)));
    }),
  CODE(">",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(GT(a, b)));
    }),
  CODE("<",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(LT(a, b)));
    }),
  CODE("<>",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!EQ(a, b)));
    }),
  CODE(">=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!LT(a, b)));
    }),
  CODE("<=",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(!GT(a, b)));
    }),
  CODE("u<",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(UINT(a) < UINT(b)));
    }),
  CODE("u>",
    {
      DU b = POP();
      DU a = POP();
      PUSH(BOOL(UINT(a) > UINT(b)));
    }),
  CODE("dup",
    {
      DU a = POP();
      PUSH(a);
      PUSH(a);
    }),
  CODE("drop", { POP(); }),
  CODE("swap",
    {
      DU a = POP();
      DU b = POP();
      PUSH(a);
      PUSH(b);
    }),
  CODE("over",
    {
      DU a = POP();
      DU b = POP();
      PUSH(b);
      PUSH(a);
      PUSH(b);
    }),
  CODE("rot",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      PUSH(b);
      PUSH(a);
      PUSH(c);
    }),
  CODE("pick",
    {
      DU n = POP();
      if (n < 0 || n >= (DU)ss.size()) throw runtime_error("pick out of range");
      DU v = ss[ss.size() - 1 - n];
      PUSH(v);
    }),
  CODE("nip",
    {
      DU a = POP();
      POP();
      PUSH(a);
    }),
  CODE("?dup",
    {
      DU a = POP();
      if (a != DU0) {
        PUSH(a);
        PUSH(a);
      }
      else
        PUSH(a);
    }),

  CODE("2dup",
    {
      DU a = POP();
      DU b = POP();
      PUSH(b);
      PUSH(a);
      PUSH(b);
      PUSH(a);
    }),
  CODE("2drop",
    {
      POP();
      POP();
    }),
  CODE("2swap",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      DU d = POP();
      PUSH(c);
      PUSH(d);
      PUSH(a);
      PUSH(b);
    }),
  CODE("2over",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      DU d = POP();
      PUSH(c);
      PUSH(d);
      PUSH(a);
      PUSH(b);
      PUSH(c);
      PUSH(d);
    }),
  CODE(">r", rs.push(POP())),
  CODE("r>", PUSH(rs.pop())),
  CODE("r@", PUSH(rs[-1])),
  CODE("base", PUSH(BASE)),
  CODE("decimal", fout << setbase(BASE = 10)),
  CODE("hex", fout << setbase(BASE = 16)),
  CODE("bl", PUSH(0x20)),
  CODE("cr", fout << ENDL),
  CODE(".", fout << setbase(BASE) << POP() << " "),
  CODE(".r",
    {
      DU w = POP();
      DU v = POP();
      fout << setbase(BASE) << setw(w) << v;
    }),
  CODE("u.r",
    {
      DU w = POP();
      DU v = POP();
      fout << setbase(BASE) << setw(w) << ABS(v);
    }),
  CODE("key", PUSH(word()[0])),
  CODE("emit", fout << (char)POP()),
  CODE("space", fout << " "),
  CODE("spaces",
    {
      DU n = POP();
      fout << setw(n) << "";
    }),
  CODE("type",
    {
      DU len = POP();
      const char* addr = reinterpret_cast<const char*>(POP());
      for (int i = 0; i < len; i++) {
        fout << addr[i];
      }
    }),
  IMMD("(", word(')')),
  IMMD(".(", fout << word(')')),
  IMMD("\\",
    {
      string s;
      getline(fin, s, '\n');
    }),
  IMMD(".\"",
    {
      string s = word('"').substr(1);
      last->append(new Str(s));
    }),
  IMMD("s\"",
    {
      string s = word('"').substr(1);
      if (compile) {
        last->append(new Str(s, last->token, last->pf.size()));
      }
      else {
        int len = s.length();
        size_t copy_len = std::min(PAD_SIZE - 1, len);
        pad_ptr = PAD_SIZE - 1 - len;
        char* addr = &pad[pad_ptr];
        memcpy(addr, s.c_str(), copy_len);
        pad[PAD_SIZE - 1] = '\0';
        PUSH((DU)addr);
        PUSH(len);
      }
    }),
  IMMD("abort\"",
    {
      string s = word('"').substr(1);
      if (compile) {
        Code* c = new Code((new string(s))->c_str(), "", _abort, 0);
        c->is_str = true;
        last->append(c);
      }
      else {
        ss.clear();
        rs.clear();
        throw runtime_error(s);
      }
    }),
  CODE("abort",
    {
      ss.clear();
      rs.clear();
      throw runtime_error("Aborted");
    }),
  CODE("<#", { pad_ptr = PAD_SIZE - 1; }),
  CODE("#",
    {
      DU n = POP();
      DU base = BASE;
      DU digit = n % base;
      n /= base;
      if (pad_ptr == 0) throw runtime_error("PAD overflow");
      char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
      pad[--pad_ptr] = ch;
      PUSH(n);
    }),
  CODE("#s",
    {
      DU n = POP();
      DU base = BASE;
      while (n != 0) {
        DU digit = n % base;
        n /= base;
        if (pad_ptr == 0) throw runtime_error("PAD overflow");
        char ch = (digit < 10) ? (char)('0' + digit) : (char)('A' + digit - 10);
        pad[--pad_ptr] = ch;
      }
      PUSH(n);
    }),
  CODE("#>",
    {
      DU n = POP();
      (void)n;
      PUSH((DU)(pad + pad_ptr));
      PUSH(PAD_SIZE - pad_ptr - 1);
    }),
  CODE("hold",
    {
      char ch = (char)POP();
      if (pad_ptr == 0) throw runtime_error("PAD overflow");
      pad[--pad_ptr] = ch;
    }),
  IMMD("if", last->append(new Bran(_if)); DICT_PUSH(new Tmp())),
  IMMD("else",
    {
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      b->stage = 1;
    }),
  IMMD("then",
    {
      Code* b = BRAN_TGT();
      int s = b->stage;
      if (s == 0) {
        b->pf.merge(last->pf);
        DICT_POP();
      }
      else {
        b->p1.merge(last->pf);
        if (s == 1) DICT_POP();
      }
    }),
  IMMD("begin", last->append(new Bran(_begin)); DICT_PUSH(new Tmp())),
  IMMD("while",
    {
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      b->stage = 2;
    }),
  IMMD("repeat",
    {
      Code* b = BRAN_TGT();
      b->p1.merge(last->pf);
      DICT_POP();
    }),
  IMMD("again",
    {
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      DICT_POP();
      b->stage = 1;
    }),
  IMMD("until",
    {
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      DICT_POP();
    }),
  IMMD("do", last->append(new Bran(_tor2)); last->append(new Bran(nullptr)); DICT_PUSH(new Tmp())),
  CODE("i", PUSH(rs[-1])),
  CODE("leave",
    {
      rs.pop();
      rs.pop();
      UNNEST();
    }),
  IMMD("loop",
    {
      Code* b = BRAN_TGT();
      b->xt = _loop;
      b->name = "loop";
      b->pf.merge(last->pf);
      DICT_POP();
    }),
  IMMD("+loop",
    {
      Code* b = BRAN_TGT();
      b->xt = _plus_loop;
      b->name = "+loop";
      b->pf.merge(last->pf);
      DICT_POP();
    }),
  CODE("exit", UNNEST()),
  CODE("[", compile = false),
  CODE("]", compile = true),
  CODE(":",
    {
      DICT_PUSH(new Code(word()));
      compile = true;
    }),
  IMMD(";", compile = false),
  CODE("constant",
    {
      DICT_PUSH(new Code(word()));
      DU v = POP();
      last->append(new Lit(v));
    }),
  CODE("variable",
    {
      DICT_PUSH(new Code(word()));
      DU addr = (DU)&heap[heap_ptr];
      ALLOT(sizeof(DU));
      *(DU*)addr = 0;
      last->append(new Var(addr));
    }),
  CODE("immediate", last->immd = 1),
  CODE("execute",
    {
      Code* w = reinterpret_cast<Code*>(POP());
      w->exec();
    }),
  CODE("create",
    {
      DICT_PUSH(new Code(word()));
      last->append(new Var((DU)&heap[heap_ptr]));
    }),
  IMMD("does>", last->append(new Bran(_does)); last->pf[-1]->token = last->token),
  CODE("@",
    {
      DU addr = POP();
      PUSH(*(DU*)addr);
    }),
  CODE("!",
    {
      DU addr = POP();
      DU val = POP();
      *(DU*)addr = val;
    }),
  CODE("c@",
    {
      DU addr = POP();
      PUSH(*(char*)addr);
    }),
  CODE("c!",
    {
      DU addr = POP();
      DU val = POP();
      *(char*)addr = (char)(val & 0xFF);
    }),
  CODE("+!",
    {
      DU addr = POP();
      DU val = POP();
      *(DU*)addr += val;
    }),
  CODE("?",
    {
      DU addr = POP();
      fout << *(DU*)addr << " ";
    }),
  CODE(",",
    {
      DU val = POP();
      *(DU*)&heap[heap_ptr] = val;
      ALLOT(sizeof(DU));
    }),
  CODE("cells",
    {
      DU val = POP();
      PUSH(val * sizeof(DU));
    }),
  CODE("allot",
    {
      DU n = POP();
      ALLOT(n);
    }),
  CODE("here", PUSH((DU)&heap[heap_ptr])),
  CODE("'",
    {
      Code* w = find(word());
      if (w) PUSH(reinterpret_cast<DU>(w));
    }),
  CODE(".s", ss_dump(BASE)),
  CODE("words", words()),
  CODE("see",
    {
      Code* w = find(word());
      if (w) {
        see(w);
      }
      else
        throw std::runtime_error("Undefined word");
    }),
  CODE("depth", PUSH(ss.size())),

  CODE("mstat", mem_stat()),
  CODE("ms", PUSH(millis())),
  CODE("rnd", PUSH(RND())),
  CODE("delay", delay(UINT(POP()))),
  CODE("included",
    {
      size_t len = (size_t)(POP());
      (void)len;
      const char* filename = reinterpret_cast<const char*>(static_cast<uintptr_t>(POP()));
      load(filename);
    }),
  CODE("forget",
    {
      Code* w = find(word());
      if (!w) return;
      int t = max((int)w->token, find("boot")->token + 1);
      for (int i = dict.size(); i > t; i--)
        DICT_POP();
    }),
  CODE("boot",
    {
      int t = find("boot")->token + 1;
      for (int i = dict.size(); i > t; i--)
        DICT_POP();
    }),
};

Code::Code(const char* s, const char* d, XT fp, U32 a)
  : name(s),
    desc(d),
    xt(fp),
    attr(a)
{
}

Code::Code(string s, bool n)
{
  Code* w = find(s);
  name = (new string(s))->c_str();
  desc = "";
  xt = w ? w->xt : nullptr;
  token = n ? dict.size() : 0;
  if (n && w) {
    fout << "redefined " << s << " ";
  }
}

void _does(Code* c)
{
  bool hit = false;
  for (Code* w : dict[c->token]->pf) {
    if (hit) last->append(w);
    if (STRCMP(w->name, "does>") == 0) hit = true;
  }
  throw 0;
}

void _str(Code* c)
{
  if (c->is_str) {
    PUSH((DU)c->name);
    PUSH(strlen(c->name));
  }
  else {
    fout << c->name;
  }
}

void _lit(Code* c)
{
  PUSH(c->q[0]);
}

void _var(Code* c)
{
  PUSH(c->q[0]);
}

void _tor(Code* c)
{
  rs.push(POP());
}

void _tor2(Code* c)
{
  DU first = POP();
  DU limit = POP();
  rs.push(limit);
  rs.push(first);
}

void _if(Code* c)
{
  if (POP()) {
    for (Code* w : c->pf)
      w->exec();
  }
  else {
    for (Code* w : c->p1)
      w->exec();
  }
}

void _begin(Code* c)
{
  int b = c->stage;
  while (true) {
    for (Code* w : c->pf)
      w->exec();
    if (b == 0 && POP() != 0) break;
    if (b == 1) continue;
    if (b == 2 && POP() == 0) break;
    for (Code* w : c->p1)
      w->exec();
  }
}

void _loop(Code* c)
{
  try {
    do {
      for (Code* w : c->pf)
        w->exec();
    } while ((rs[-1] += 1) < rs[-2]);
    rs.pop();
    rs.pop();
  }
  catch (int) {
  }
}

void _plus_loop(Code* c)
{
  try {
    do {
      for (Code* w : c->pf)
        w->exec();
      DU n = POP();
      DU index = rs[-1];
      DU limit = rs[-2];
      index += n;
      rs[-1] = index;
      if (n >= 0) {
        if (index >= limit) break;
      }
      else {
        if (index <= limit) break;
      }
    } while (true);
    rs.pop();
    rs.pop();
  }
  catch (int) {
    rs.pop();
    rs.pop();
  }
}

void _abort(Code* c)
{
  ss.clear();
  rs.clear();
  throw runtime_error(c->name ? c->name : "Aborted");
}

string word(char delim)
{
  string s;
  delim ? getline(fin, s, delim) : fin >> s;
  return s;
}

void ss_dump(DU base)
{
  char buf[34];
  auto rdx = [&buf](DU v, int b) -> const char* {
    int i = 33;
    buf[i] = '\0';
    int dec = (b == 10);
    U32 n = dec ? UINT(ABS(v)) : UINT(v);
    do {
      U8 d = (U8)MOD(n, b);
      n /= b;
      buf[--i] = d > 9 ? (d - 10) + 'a' : d + '0';
    } while (n && i);
    if (dec && v < DU0) buf[--i] = '-';
    return &buf[i];
  };

  fout << "<" << ss.size() << "> ";
  for (DU v : ss) {
    fout << rdx(v, base) << ' ';
  }
}

void _see(Code* c)
{
  if (!c) return;
  if (c->xt == _lit) {
    fout << c->q[0] << " ";
    return;
  }
  const char* nm = c->name ? c->name : "";
  if (strcmp(nm, "if") == 0) {
    fout << "if ";
    for (Code* w : c->pf)
      _see(w);
    if (c->stage == 1 && !c->p1.empty()) {
      fout << "else ";
      for (Code* w : c->p1)
        _see(w);
    }
    fout << "then ";
    return;
  }
  if (strcmp(nm, "begin") == 0) {
    fout << "begin ";
    for (Code* w : c->pf)
      _see(w);
    if (c->stage == 2) {
      fout << "while ";
      for (Code* w : c->p1)
        _see(w);
      fout << "repeat ";
    }
    else if (c->stage == 0) {
      fout << "until ";
    }
    else if (c->stage == 1) {
      fout << "again ";
    }
    return;
  }
  if (strcmp(nm, "do") == 0) {
    fout << "do ";
    return;
  }
  if (strcmp(nm, "loop") == 0 || strcmp(nm, "+loop") == 0) {
    for (Code* w : c->pf)
      _see(w);
    fout << nm << " ";
    return;
  }
  if (nm[0] != '\0' && nm[0] != '\t') {
    fout << nm << " ";
  }
}

void see(Code* c)
{
  if (!c) {
    fout << "  -> { not found }";
    return;
  }
  if (c->xt) {
    fout << "  ->{ " << c->desc << "; } ";
    return;
  }
  fout << ": " << c->name << " ";
  for (Code* w : c->pf)
    _see(w);
  fout << "; ";
}

void words()
{
  const int WIDTH = 60;
  int x = 0;
  fout << setbase(16) << setfill('0');
  for (Code* w : dict) {
#if CC_DEBUG > 1
    fout << setw(4) << w->token << "> " << (UFP)w << ' ' << setw(8) << (U32)(UFP)w->xt << (w->is_str ? '"' : ':')
         << (w->immd ? '*' : ' ') << w->name << "  " << ENDL;
#else
    fout << "  " << w->name;
    x += (strlen(w->name) + 2);
    if (x > WIDTH) {
      fout << ENDL;
      x = 0;
    }
#endif
  }
  fout << setfill(' ') << setbase(BASE) << ENDL;
}

void load(const char* fn)
{
  void (*cb)(int, const char*) = fout_cb;
  string in;
  getline(fin, in);
  if (!forth_include(fn)) {
    throw runtime_error("Can't open file");
  }
  fout_cb = cb;
  fin.clear();
  fin.str(in);
}

Code* find(string s)
{
  for (int i = dict.size() - 1; i >= 0; --i) {
    if (STRCMP(s.c_str(), dict[i]->name) == 0) return dict[i];
  }
  return nullptr;
}

DU parse_number(string idiom)
{
  const char* cs = idiom.c_str();
  int b = BASE;
  switch (*cs) {
  case '%':
    b = 2;
    cs++;
    break;
  case '&':
  case '#':
    b = 10;
    cs++;
    break;
  case '$':
    b = 16;
    cs++;
    break;
  }
  char* p;
  errno = 0;
#if DU == float
  DU n = (b == 10) ? strtof(cs, &p) : strtol(cs, &p, b);
#else
  DU n = strtol(cs, &p, b);
#endif
  if (errno || *p != '\0') throw runtime_error("Undefined word");
  return n;
}

void forth_core(string idiom)
{
  Code* w = find(idiom);
  if (w) {
    if (compile && !w->immd)
      last->append(w);
    else
      w->exec();
    return;
  }
  DU n = parse_number(idiom);
  if (compile)
    last->append(new Lit(n));
  else
    ss.push(n);
}

void forth_init()
{
  const int sz = sizeof(rom) / sizeof(Code);
  dict.reserve(sz * 2);
  for (int i = 0; i < sz; ++i) {
    DICT_PUSH((Code*)&rom[i]);
  }
  heap.resize(HEAP_SIZE);
  heap_ptr = sizeof(DU);
  BASE = 10;
  pad_ptr = PAD_SIZE - 1;
  pad[PAD_SIZE - 1] = '\0';
}

int forth_vm(const char* cmd, void (*hook)(int, const char*))
{
  static uint err_cnt = 0;
  bool error_occured = false;

  auto next_token = [](const string& line, size_t start_pos) -> string {
    size_t i = start_pos;
    while (i < line.size() && isspace(line[i]))
      i++;
    if (i >= line.size()) return "";
    size_t j = i;
    while (j < line.size() && !isspace(line[j]))
      j++;
    return line.substr(i, j - i);
  };

  auto replace = [&](const string& line, const string& idiom, const string& exception_what) -> string {
    string ret = line;
    if (exception_what != "Undefined word") {
      size_t pos = line.find(idiom);
      if (pos != string::npos)
        ret.replace(pos, idiom.length(), ">>>" + idiom + "<<<");
      else
        ret = ">>>" + idiom + "<<<";
    }
    else if (idiom == "see") {
      size_t pos = line.find("see");
      if (pos != string::npos) {
        string nxt = next_token(line, pos + 3);
        if (!nxt.empty()) {
          size_t nxt_pos = line.find(nxt, pos + 3);
          if (nxt_pos != string::npos)
            ret.replace(nxt_pos, nxt.length(), ">>>" + nxt + "<<<");
          else
            ret = ">>>" + nxt + "<<<";
        }
        else {
          ret = line + " >>><<<";
        }
      }
      else {
        ret = ">>>" + idiom + "<<<";
      }
    }
    else {
      size_t pos = line.find(idiom);
      if (pos != string::npos)
        ret.replace(pos, idiom.length(), ">>>" + idiom + "<<<");
      else
        ret = ">>>" + idiom + "<<<";
    }
    return ret;
  };

  auto output = [&]() {
    if (fout_cb && !fout.str().empty()) {
      fout_cb((int)fout.str().length(), fout.str().c_str());
      fout.str("");
    }
  };

  auto outer = [&](const string& current_line) {
    string idiom;
    while (fin >> idiom) {
      try {
        forth_core(idiom);
      }
      catch (exception& e) {
        err_cnt++;
        error_occured = true;
        fout << ENDL;
        fout << ":" << err_cnt << ": " << e.what() << ENDL;
        string marked_line = replace(current_line, idiom, e.what());
        fout << marked_line << ENDL;
        output();
        compile = false;
        getline(fin, idiom, '\n');
      }
      catch (int) {
        output();
      }
    }
  };

  auto cb = [](int, const char* rst) { cout << rst; };
  fout_cb = hook ? hook : cb;
  istringstream istm(cmd);
  string line;
  fout.str("");

  while (getline(istm, line)) {
    fin.clear();
    fin.str(line);
    outer(line);
  }

  if (!error_occured) {
    if (compile) {
      fout << " compiled" << ENDL;
    }
    else {
      fout << " ok" << ENDL;
    }
  }
  output();
  return 0;
}
