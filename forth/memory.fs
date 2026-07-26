\ Copyright (c) 2026 Vladimir Egorov 
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

: word-dump ( n-cells addr -- )
  {: n-cells addr :}
  n-cells 0 ?do addr i cells + ? loop ;

: dump ( n-bytes addr -- )
  {: n-bytes addr :}
  n-bytes 0 ?do addr i + c@ . loop ;

: word-memset ( val n-cells addr -- )
  {: val n-cells addr :}
  n-cells 0 ?do val addr i cells + ! loop ;

: memset ( val n-bytes addr -- )
  {: val n-bytes addr :}
  n-bytes 0 ?do val 255 and addr i + c! loop ;

: memcpy ( to from n-bytes -- )
  {: to from n-bytes :} 
  n-bytes 0 ?do from i + c@ to i + c! loop ;

: memcmp ( addr1 addr2 n-bytes -- n )
  {: addr1 addr2 n-bytes -- n :}
  0 -> n
  n-bytes 0 ?do
    addr1 i + c@ addr2 i + c@ - dup if -> n leave else drop then
  loop n ;
