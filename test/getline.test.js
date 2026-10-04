import assert from "node:assert/strict";
import { test } from "node:test";
import {
  assertDeterministicEdit,
  assertStatus,
  captureParse,
  clean,
  dirty,
  writeSource,
} from "./support/parser.js";

const validExpressions = [
  "-getline x++",
  "a + getline x++",
  "a getline x++",
  "q = $-getline x++",
  "a ^ getline x++",
  "getline $-getline x++",
  "++$getline x--",
  "print $-getline x++ tline y-- z",
  "x getline y++ | getline",
  "print $getline x++",
  "print $-getline a[1]-- ++y z",
  "getline $-x++",
  "getline (x)",
  '$getline < "f" ++',
  '$getline < "f" --',
  '$getline < ("f") ++',
  "$getline < -1 ++",
  "$getline < f() ++",
  "$getline < /f/ ++",
  "$getline < ++y ++",
  '$getline x < "f" ++',
  '$getline $-x < "f" ++',
  'print $getline < "f" ++',
  'printf $getline $-x < "f" --',
  '$getline < "f" ++ | getline',
  '$getline < "f" ++y',
  "$getline < y++",
  "$getline < y++++",
  "$getline < y + 1 ++",
  '$getline < x = "f" ++',
  '$getline < a < "f" ++',
  "$getline < getline x++",
  "$getline < a < getline x++ < y",
  "$getline < a < getline x-- < y",
  "$getline < a < $getline < b < getline x++++ < y",
  "$getline < a < $getline < b < getline x++++++ < y",
  "getline $getline < a < getline x++ < y",
  "print ($getline < a < getline x++ < y)",
  "f($getline < a < getline x++ < y)",
  "x[$getline < a < getline x++ < y]",
  "$getline < f(getline x++)++ < y",
  '$getline $getline < "f" ++',
  'getline $getline $-x < "f" ++',
  'getline line < dir "/" name',
  "getline < file > 0",
  "getline x++ $getline y++",
  "getline a[i/2]++ y",
  "getline a[i++ / 2]++",
  'getline $getline < "f"',
  'getline $getline < "f" ++',
  "getline $getline < x++",
  "getline $getline < x++++",
  "getline $getline < x | getline ++",
  "getline $getline < a ? x : (y)++",
  "getline $getline < 1e++",
  "getline $getline < a && # source\n x++",
  "getline $getline < 1 &&\\\n\n1++",
  "getline $getline < 1 ||\\\n# source\n1--",
  "getline $getline < 1 && # first\n\\\n# second\n1++",
  "getline x++ $getline $getline < 1 ++",
  "getline $getline < getline $getline < x++++",
  "getline $getline < $getline < 1 in a 1++",
  'getline $getline < $getline < x in a < "f" ++',
  "getline $getline < x < y < z | getline ++",
  "getline $getline < x ~ y ~ 1 | getline ++",
  'getline $getline < x ~ y | getline z ~ "f" ++',
  "getline $getline $-x < x | getline ++",
];

for (const expression of validExpressions) {
  test(`posix_awk: each single-byte repair restores ${expression}`, () => {
    const source = `{ ${expression} }\n`;
    const fresh = captureParse(writeSource("getline-probe", "fresh", source));
    assertStatus(expression, fresh, 0);
    clean(fresh.tree);
    for (let byte = 0; byte < source.length; byte += 1) {
      const initial = source.slice(0, byte) + source.slice(byte + 1);
      assertDeterministicEdit(
        `restore byte ${byte} in getline expression`,
        initial,
        source,
        [{ byte, deleteBytes: 0, insert: source[byte] }],
      );
    }
  });
}

for (const expression of [
  "print getline x++",
  "getline x++++",
  "a + (1,2) in array",
  "$getline $-x^2++",
  'getline < "f" ++',
  '$getline < "f" ++--',
  "$getline < y++++++",
]) {
  test(`posix_awk: reject ${expression}`, () => {
    const result = captureParse(
      writeSource("getline-probe", "invalid", `{ ${expression} }\n`),
    );
    assert.ok(result.status === 0 || result.status === 1);
    dirty(result.tree);
  });
}
