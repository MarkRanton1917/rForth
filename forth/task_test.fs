: task1_abort 4 0 do ." t2: " i dup . cr 3 = if abort" Task1 aborted" then 150 delay loop ." t2 done" cr ;
: task2 5 0 do ." t1: " i . cr 100 delay loop ." t1 done" cr ;
variable tid1
variable tid2
: main_abort ['] task1_abort task tid1 ! ['] task2 task tid2 !
  begin
    pause
    tid1 @ active? 0= tid2 @ active? 0= and
  until
  ." all done" cr
;

: task1 4 0 do ." t2: " i dup . cr 150 delay loop ." t2 done" cr ;
: main ['] task1 task tid1 ! ['] task2 task tid2 !
  begin
    pause
    tid1 @ active? 0= tid2 @ active? 0= and
  until
  ." all done" cr
;
