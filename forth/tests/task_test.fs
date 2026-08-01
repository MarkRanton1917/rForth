\ Copyright (c) 2026 Vladimir Egorov 
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

: task1_abort 4 0 do ." t2: " i dup . cr 3 = if abort" Task1 aborted" then 150 delay loop ." t2 done" cr ;
: task2 5 0 do ." t1: " i . cr 100 delay loop ." t1 done" cr ;
variable tid1
variable tid2
: main_abort 4096 ['] task1_abort task tid1 ! 4096 ['] task2 task tid2 !
  begin
    pause
    tid1 @ active? 0= tid2 @ active? 0= and
  until
  ." all done" cr
;

: task1 4 0 do ." t2: " i dup . cr 150 delay loop ." t2 done" cr ;
: main 4096 ['] task1 task tid1 ! 4096 ['] task2 task tid2 !
  begin
    pause
    tid1 @ active? 0= tid2 @ active? 0= and
  until
  ." all done" cr
;

: forever begin 50 delay again ;
variable tid3
: main_stop 4096 ['] forever task tid3 !
  100 delay
  tid3 @ active? if ." running" cr else ." NOT RUNNING" cr then
  tid3 @ stop
  200 delay
  tid3 @ active? 0= if ." stopped" cr else ." STILL RUNNING" cr then
;

: task3 5 0 do ." t3: " i . cr 50 delay i 2 = if self stop then loop ." T3 NOT STOPPED" cr ;
variable tid4
: main_self 4096 ['] task3 task tid4 !
  tid4 @ self = if ." SELF IS THE MAIN TASK" cr then
  400 delay
  tid4 @ active? 0= if ." self stopped" cr else ." STILL RUNNING" cr then
;
