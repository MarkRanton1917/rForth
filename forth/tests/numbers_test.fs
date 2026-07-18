\ Copyright (c) 2026 Vladimir Egorov
\ This library is licensed under the MIT License.
\ See the LICENSE file in the root of the repository for the full license text.

: double-input ( -- )
  ." Enter a number: "
  pad 32 accept {: len -- :}
  pad len type cr
  depth {: before -- :}
  pad len number?
  if
    depth before - {: n-cells -- :}
    n-cells 2 = if
      {: lo hi -- :}
      lo hi lo hi d+
      ." double, doubled: "
      d.
    else n-cells 1 = if
      {: n -- :}
      n n +
      ." single, doubled: "
      .
    else
      ." Unsupported number format" cr
    then then
  else
    ." Not a valid number: " type cr
  then
  cr
;
