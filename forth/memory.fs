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

( stack ) 
: stack ( n-cells -- ) 
  create dup , -1 , cells allot does> ;

: stack._push ( val stack -- ) 
  dup 1 cells + dup 1 swap +! \ increase stack pointer 
  @ 2 + cells + ! ; \ write value

: stack._pop ( stack -- val ) 
  dup dup 1 cells + @ 2 + cells + @ \ read value
  swap 1 cells + -1 swap +! ; \ decrease stack pointer

: stack.push ( val stack -- ) 
  dup dup @ 1 - >r 1 cells + @ r> < 
  if stack._push else abort" stack.push: overflow" then ;

: stack.pop ( stack -- val ) dup 1 cells + @ -1 > 
  if stack._pop else abort" stack.pop: underflow" then ;

( ring )
: ring ( n-cells -- ) 
create dup , -1 , cells allot does> ;

: ring.append ( val ring -- ) 
  dup dup @ 1 - swap 1 cells + @ > if 
    dup 1 cells + dup 1 swap +! \ increase ring pointer
    @ 2 + cells + ! \ write value
  else
    abort" ring.append: overflow" 
  then ;

: ring._decrease-pointer ( ring -- ) 
  1 cells + -1 swap +! ;

: ring.remove ( n ring -- )
  2dup 1 cells + @ dup rot < if abort" ring.remove: index out of range" then \ n < size
    dup 0= if \ ring pointer = 0
     swap ring._decrease-pointer 2drop
  else
    rot dup rot = if \ ring pointer = n
      swap ring._decrease-pointer drop
    else
      swap dup 1 cells + @ rot 
      do dup dup i 3 + cells + @ swap i 2 + cells + ! loop \ shift values to the left 
      ring._decrease-pointer 
    then
  then ;

: ring._pointer ( n ring -- p ) 
  1 cells + @ dup 0= if 2drop 0 else 1 + mod then ;

: ring.get ( n ring -- val )
  dup >r dup 1 cells + @ -1 = if abort" ring.get: underflow" then \ ring is empty
  ring._pointer 2 + cells r> + @ ;
