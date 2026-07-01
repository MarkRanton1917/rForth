\ Copyright (c) 2026 Vladimir Egorov 
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

( utilities )
: dump ( n-cells addr -- ) 
  over dup 0> if 
    0 do dup i cells + ? loop 2drop 
  then ;

: cdump ( n-bytes addr -- ) 
  over dup 0> if 
    0 do dup i + c@ . loop 2drop 
  then ;

: memset ( val n-cells addr -- ) 
  over dup 0> if 
    0 do rot swap 2dup i cells + ! rot swap loop 
  then 2drop drop ;

: cmemset ( val n-bytes addr -- ) 
  rot 255 and -rot over dup 0> if 
    0 do rot swap 2dup i + c! rot swap loop 
  then 2drop drop ;
