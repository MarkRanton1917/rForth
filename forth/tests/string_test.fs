\ Copyright (c) 2026 Vladimir Egorov
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

variable failures

: check ( flag a u -- )
  {: flag a u :}
  flag if ." ok   " else ." FAIL " failures @ 1+ failures ! then
  a u type cr
;

create buf 64 allot

: t-slice ( -- )
  s" hello" 2 /string s" llo" str= s" /string skips n bytes" check
  s" hello" 9 /string nip 0= s" /string clamps to len" check
  s" hello" -3 /string nip 5 = s" /string clamps negative" check
  s" ab   " -trailing nip 2 = s" -trailing" check
  s"    ab" -leading nip 2 = s" -leading" check
  s"   ab  " trim s" ab" str= s" trim" check
  s"      " trim nip 0= s" trim all blanks" check
  s" ab" trim s" ab" str= s" trim keeps clean string" check
;

: t-compare ( -- )
  s" abc" s" abd" compare -1 = s" compare less" check
  s" abd" s" abc" compare 1 = s" compare greater" check
  s" abc" s" abc" compare 0= s" compare equal" check
  s" abc" s" ab" compare 1 = s" compare longer" check
  s" ab" s" abc" compare -1 = s" compare shorter" check
  s" abc" s" abc" str= s" str=" check
  s" abc" s" abd" str= 0= s" str= differs" check
  s" abc" s" ab" str= 0= s" str= length differs" check
;

: t-fill ( -- )
  buf 8 blank buf c@ bl = s" blank" check
  buf 8 erase buf c@ 0= s" erase" check
;

: t-index ( -- )
  s" a,b" ascii , char-index 1 = s" char-index" check
  s" abc" ascii , char-index -1 = s" char-index missing" check
  s" ,ab" ascii , char-index 0= s" char-index first" check
;

: t-split ( -- )
  s" 12,34" ascii , split {: h hl t tl :}
  h hl s" 12" str= s" split head" check
  t tl s" 34" str= s" split tail" check
  s" ab," ascii , split 2swap 2drop nip 0= s" split empty tail" check
  s" ab" ascii , split 2swap 2drop nip 0= s" split no separator" check
  s" ab" ascii , split 2drop s" ab" str= s" split no separator head" check
;

: t-search ( -- )
  s" hello world" s" wor" search {: a u f :}
  f s" search found" check
  a u s" world" str= s" search tail" check
  s" hello" s" xyz" search nip nip 0= s" search missing flag" check
  s" hello" s" xyz" search drop nip 5 = s" search missing keeps string" check
  s" hello" s" hello" search nip nip s" search whole" check
  s" hello" s" hello!" search nip nip 0= s" search longer than string" check
;

: t-prefix ( -- )
  s" hello" s" he" starts-with? s" starts-with?" check
  s" hello" s" lo" starts-with? 0= s" starts-with? no" check
  s" he" s" hello" starts-with? 0= s" starts-with? too long" check
  s" hello" s" lo" ends-with? s" ends-with?" check
  s" hello" s" he" ends-with? 0= s" ends-with? no" check
;

: t-build ( -- )
  s" ab" buf str! {: n :}
  s" cd" buf n str+ -> n
  buf n s" abcd" str= s" str! and str+" check
  n 4 = s" str+ returns length" check
;

: t-numbers ( -- )
  s" 1A" parse-hex {: v ok :}
  ok v 26 = and s" parse-hex" check
  s" deadbeef" parse-hex drop $deadbeef = s" parse-hex long" check
  s" 1G" parse-hex nip 0= s" parse-hex rejects" check
  s" " parse-hex nip 0= s" parse-hex rejects empty" check
  -42 buf n>str s" -42" str= s" n>str negative" check
  42 buf n>str s" 42" str= s" n>str positive" check
  0 buf n>str s" 0" str= s" n>str zero" check
;

: t-case ( -- )
  s" aB3" buf str! {: n :}
  buf n >upper buf n s" AB3" str= s" >upper" check
  buf n >lower buf n s" ab3" str= s" >lower" check
;

: t-buffers ( -- )
  s" scratch" pad str! {: n :}
  1234 buf n>str 2drop
  pad n s" scratch" str= s" pictured output leaves pad alone" check
;

: t-utf8 ( -- )
  s" Привет" u8-len 6 = s" u8-len" check
  s" hello" u8-len 5 = s" u8-len ascii" check
  s" Привет" 3 u8-trunc nip 6 = s" u8-trunc keeps sequences" check
  s" Привет" 99 u8-trunc nip 12 = s" u8-trunc clamps" check
  s" Привет" 0 u8-trunc nip 0= s" u8-trunc zero" check
  s" П" drop u8-size 2 = s" u8-size two byte" check
  s" a" drop u8-size 1 = s" u8-size ascii" check
;

t-slice t-compare t-fill t-index t-split t-search t-prefix t-build t-numbers t-case t-buffers t-utf8

\ Interpreted, where s" hands out a transient buffer instead of compiling a literal.
s" abc" s" abd" str= 0= s" interpreted literals do not alias" check
s" abc" s" abc" str= s" interpreted literals compare by value" check
0 pad n>str s" 0" str= s" n>str zero interpreted" check

depth 0= s" stack balanced" check

cr failures @ . ." failure(s)" cr
