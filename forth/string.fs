\ Copyright (c) 2026 Vladimir Egorov
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

\ Strings are (addr len) pairs. Requires memory.fs for memcpy, memcmp and memset.
\ Words that build a result take the destination buffer as an argument: <# ... #> leaves its
\ string in a transient buffer that the next such sequence overwrites.

: space? ( c -- flag )
  {: c :}
  c bl = c 9 = or c 10 = or c 13 = or
;

: upc ( c -- c' )
  {: c :}
  c ascii a >= c ascii z <= and if c 32 - else c then
;

: lwc ( c -- c' )
  {: c :}
  c ascii A >= c ascii Z <= and if c 32 + else c then
;

\ ASCII only: applied byte by byte, so UTF-8 text above U+007F is left alone rather than mangled.
: >upper ( a u -- )
  {: a u :}
  u 0 ?do a i + c@ upc a i + c! loop
;

: >lower ( a u -- )
  {: a u :}
  u 0 ?do a i + c@ lwc a i + c! loop
;

: /string ( a u n -- a' u' )
  {: a u n :}
  n 0 max u min -> n
  a n + u n -
;

: -leading ( a u -- a' u' )
  {: a u -- done :}
  begin done invert u 0> and while
    a c@ space? if a 1+ -> a u 1- -> u else -1 -> done then
  repeat
  a u
;

: -trailing ( a u -- a u' )
  {: a u -- done :}
  begin done invert u 0> and while
    a u 1- + c@ space? if u 1- -> u else -1 -> done then
  repeat
  a u
;

: trim ( a u -- a' u' )
  -leading -trailing
;

: compare ( a1 u1 a2 u2 -- n )
  {: a1 u1 a2 u2 -- d :}
  a1 a2 u1 u2 min memcmp -> d
  d 0= if u1 u2 - -> d then
  d 0< if -1 else d 0> if 1 else 0 then then
;

: str= ( a1 u1 a2 u2 -- flag )
  {: a1 u1 a2 u2 :}
  u1 u2 = if a1 a2 u1 memcmp 0= else 0 then
;

: blank ( a u -- )
  {: a u :}
  bl u a memset
;

: erase ( a u -- )
  {: a u :}
  0 u a memset
;

: char-index ( a u c -- i | -1 )
  {: a u c -- idx :}
  -1 -> idx
  u 0 ?do
    a i + c@ c = if i -> idx leave then
  loop
  idx
;

: split ( a u c -- head hlen tail tlen )
  {: a u c -- i :}
  a u c char-index -> i
  i 0< if
    a u  a u +  0
  else
    a i  a i + 1+  u i - 1-
  then
;

: search ( a1 u1 a2 u2 -- a3 u3 flag )
  {: a u pa pu -- idx :}
  -1 -> idx
  pu 0= if 0 -> idx then
  pu 0<> pu u <= and if
    u pu - 1+ 0 ?do
      a i + pa pu memcmp 0= if i -> idx leave then
    loop
  then
  idx 0< if a u 0 else a idx + u idx - -1 then
;

: starts-with? ( a1 u1 a2 u2 -- flag )
  {: a u pa pu :}
  pu u <= if a pa pu memcmp 0= else 0 then
;

: ends-with? ( a1 u1 a2 u2 -- flag )
  {: a u pa pu :}
  pu u <= if a u pu - + pa pu memcmp 0= else 0 then
;

: str! ( src len dst -- len )
  {: src len dst :}
  dst src len memcpy
  len
;

: str+ ( src len dst dlen -- len' )
  {: src len dst dlen :}
  dst dlen + src len memcpy
  dlen len +
;

: n>str ( n dst -- dst len )
  {: n dst -- a len :}
  <# n abs #s n sign #> -> len -> a
  dst a len memcpy
  dst len
;
