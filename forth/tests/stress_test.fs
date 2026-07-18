\ Copyright (c) 2026 Vladimir Egorov
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

: mk create , does> @ ;
1 mk w1
2 mk w2
3 mk w3
w1 . w2 . w3 .
forget w2
w1 .
: outer2 mk mk mk ;
forget mk
: sq dup * ;
: cb dup dup * * ;
5 sq . 3 cb .
forget sq
: sq dup * ;
: sq dup * ;
: sq dup * ;
6 sq .
see sq
words
: rec dup 0 > if dup 1- rec then ;
5 rec drop drop drop drop drop drop
: loopy 10 0 do i loop drop drop drop drop drop drop drop drop drop drop ;
loopy
forget rec
boot
words
