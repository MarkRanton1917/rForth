\ Copyright (c) 2026 Vladimir Egorov
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

variable fid

s" /tmp/rforth_file_test.txt" r/w create-file throw fid !
s" hello world" fid @ write-line throw
fid @ close-file throw

s" /tmp/rforth_file_test.txt" r/o open-file throw fid !
pad 256 fid @ read-line throw
." flag=" . cr
." read: " pad swap type cr
fid @ close-file throw

s" /tmp/rforth_file_test.txt" delete-file throw

s" /tmp/rforth_file_test.txt" r/o open-file
." reopen after delete ior=" . cr
drop

." file test ok" cr
