///
/// @file
/// @brief eForth - C++ vector-based object-threaded implementation (no TOS)
///
///====================================================================
#include <sstream>
#include <cstring>
#include "ceforth.h"

using namespace std;

FV<Code*> dict;
FV<DU> ss;
FV<DU> rs;
bool compile = false;
Code* last;

istringstream fin;
ostringstream fout;
string pad;
void (*fout_cb)(int, const char*);

#define POP()  (ss.pop())
#define PUSH(v) (ss.push(v))
#define BOOL(f) ((f) ? -1 : 0)
#define VAR(i_w) (*(dict[(int)((UINT(i_w)) & 0xffff)]->pf[0]->q.data() + ((UINT(i_w)) >> 16)))
#define DICT_PUSH(c) (dict.push(last = (c)))
#define DICT_POP()   (dict.pop(), last = dict[-1])
#define BRAN_TGT()   (dict[-2]->pf[-1])
#define BASE (VAR(0))
#define STR(i_w) (EQ(i_w, UINT(-DU1)) ? pad.c_str() : dict[(UINT(i_w)) & 0xffff]->pf[(UINT(i_w)) >> 16]->name)
#define UNNEST() throw 0

void _if();
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
#if USE_FLOAT
  CODE("int",
    {
      DU a = POP();
      PUSH(a < DU0 ? -DU1 * UINT(-a) : UINT(a));
    }),
#endif

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
  CODE("-rot",
    {
      DU a = POP();
      DU b = POP();
      DU c = POP();
      PUSH(a);
      PUSH(c);
      PUSH(b);
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

  CODE("base", PUSH(0)),
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
      DU i_w = UINT(POP());
      string s = STR(i_w);
      fout << s.substr(0, (size_t)len);
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
        pad = s;
        PUSH(-DU1);
        PUSH(s.length());
      }
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

  IMMD("for", last->append(new Bran(_tor)); last->append(new Bran(_for)); DICT_PUSH(new Tmp())),
  IMMD("aft",
    {
      Code* b = BRAN_TGT();
      b->pf.merge(last->pf);
      b->stage = 3;
    }),
  IMMD("next",
    {
      Code* b = BRAN_TGT();
      if (b->stage == 0)
        b->pf.merge(last->pf);
      else
        b->p2.merge(last->pf);
      DICT_POP();
    }),

  IMMD("do", last->append(new Bran(_tor2)); last->append(new Bran(_loop)); DICT_PUSH(new Tmp())),
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
      Code* w = last->append(new Lit(v));
      w->pf[0]->token = w->token;
    }),
  CODE("variable",
    {
      DICT_PUSH(new Code(word()));
      Code* w = last->append(new Var(DU0));
      w->pf[0]->token = w->token;
    }),
  CODE("immediate", last->immd = 1),

  CODE("exec", dict[UINT(POP())]->exec()),
  CODE("create",
    {
      DICT_PUSH(new Code(word()));
      Code* w = last->append(new Var(DU0));
      w->pf[0]->token = w->token;
      w->pf[0]->q.pop();
    }),
  IMMD("does>", last->append(new Bran(_does)); last->pf[-1]->token = last->token),
  CODE("to",
    {
      Code* w = find(word());
      if (!w) return;
      VAR(w->token) = POP();
    }),
  CODE("is",
    {
      DICT_PUSH(new Code(word(), false));
      int w = UINT(POP());
      last->xt = dict[w]->xt;
      last->pf = dict[w]->pf;
    }),

  CODE("@",
    {
      U32 i_w = UINT(POP());
      PUSH(VAR(i_w));
    }),
  CODE("!",
    {
      U32 i_w = UINT(POP());
      VAR(i_w) = POP();
    }),
  CODE("+!",
    {
      U32 i_w = UINT(POP());
      VAR(i_w) += POP();
    }),
  CODE("?",
    {
      U32 i_w = UINT(POP());
      fout << VAR(i_w) << " ";
    }),
  CODE(",", last->pf[0]->q.push(POP())),
  CODE("cells", { /* backward compatible */ }),
  CODE("allot",
    {
      U32 n = UINT(POP());
      for (U32 i = 0; i < n; i++)
        last->pf[0]->q.push(DU0);
    }),
  CODE("th",
    {
      U32 i = UINT(POP()) << 16;
      DU w = POP();
      PUSH(UINT(w) | i);
    }),

  CODE("here", PUSH(last->token)),
  CODE("'",
    {
      Code* w = find(word());
      if (w) PUSH(w->token);
    }),
  CODE(".s", ss_dump(BASE)),
  CODE("words", words()),
  CODE("see",
    {
      Code* w = find(word());
      if (w) {
        see(w);
        fout << ENDL;
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
      POP();
      U32 i_w = UINT(POP());
      load(STR(i_w));
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
    fout << "reDef?" << ENDL;
  }
}

void _str(Code* c)
{
  if (!c->token)
    fout << c->name;
  else {
    PUSH(c->token);
    PUSH(strlen(c->name));
  }
}
void _lit(Code* c)
{
  PUSH(c->q[0]);
}
void _var(Code* c)
{
  PUSH(c->token);
}
void _tor(Code* c)
{
  rs.push(POP());
}
void _tor2(Code* c)
{
  DU limit = POP();
  DU first = POP();
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
void _for(Code* c)
{
  int b = c->stage;
  try {
    do {
      for (Code* w : c->pf)
        w->exec();
    } while (b == 0 && (rs[-1] -= 1) >= 0);
    while (b) {
      for (Code* w : c->p2)
        w->exec();
      if ((rs[-1] -= 1) < 0) break;
      for (Code* w : c->p1)
        w->exec();
    }
    rs.pop();
  }
  catch (int) {
    rs.pop();
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
void _does(Code* c)
{
  bool hit = false;
  for (Code* w : dict[c->token]->pf) {
    if (hit) last->append(w);
    if (STRCMP(w->name, "does>") == 0) hit = true;
  }
  throw 0;
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
#if USE_FLOAT
    DU t, f = modf(v, &t);
    if (ABS(f) > DU_EPS) {
      sprintf(buf, "%0.6g", v);
      return buf;
    }
#endif
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
  if (strcmp(nm, "for") == 0) {
    fout << "for ";
    for (Code* w : c->pf)
      _see(w);
    if (c->stage == 3) {
      fout << "aft ";
      for (Code* w : c->p1)
        _see(w);
      fout << "then ";
      for (Code* w : c->p2)
        _see(w);
    }
    fout << "next ";
    return;
  }
  if (strcmp(nm, "do") == 0) {
    fout << "do ";
    for (Code* w : c->pf)
      _see(w);
    fout << "loop ";
    return;
  }
  if (nm[0] != '\0' && nm[0] != '\t') {
    fout << nm << " ";
  }
}

void see(Code* c)
{
  if (!c) {
    fout << "  -> { not found }" << ENDL;
    return;
  }
  if (c->xt) {
    fout << "  ->{ " << c->desc << "; }" << ENDL;
    return;
  }
  fout << ": " << c->name << " ";
  for (Code* w : c->pf)
    _see(w);
  fout << ";" << ENDL;
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
  fout << ENDL;
  forth_include(fn);
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
  dict[0]->append(new Var(10));
}

int forth_vm(const char* cmd, void (*hook)(int, const char*))
{
  static uint err_cnt = 0;
  bool error_occured = false;
  auto outer = [&]() {
    string idiom;
    while (fin >> idiom) {
      try {
        forth_core(idiom);
      }
      catch (exception& e) {
        err_cnt++;
        error_occured = true;
        fout << ":" << err_cnt << ": " << e.what() << ENDL;
        fout << ">>>" << idiom << "<<<" << ENDL;
        if (fout_cb && !fout.str().empty()) {
          fout_cb((int)fout.str().length(), fout.str().c_str());
          fout.str("");
        }
        compile = false;
        getline(fin, idiom, '\n');
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
    outer();
  }
  if (!error_occured) fout << " ok" << ENDL;
  if (fout_cb && !fout.str().empty()) {
    fout_cb((int)fout.str().length(), fout.str().c_str());
    fout.str("");
  }
  return 0;
}
