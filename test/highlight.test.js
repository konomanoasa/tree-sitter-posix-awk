import assert from "node:assert/strict";
import {
  mkdirSync,
  mkdtempSync,
  rmSync,
  symlinkSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { after, before, test } from "node:test";
import {
  createTreeSitter,
  grammars,
  packageName,
  root,
} from "../scripts/tree-sitter.js";

function decodeEntities(text) {
  return text
    .replaceAll("&lt;", "<")
    .replaceAll("&gt;", ">")
    .replaceAll("&quot;", '"')
    .replaceAll("&#39;", "'")
    .replaceAll("&amp;", "&");
}

function renderedCaptures(html, source) {
  const start = html.indexOf("<pre><code>");
  const end = html.indexOf("</code></pre>");
  assert.ok(start >= 0 && end >= start, html);
  const content = html.slice(start + "<pre><code>".length, end);
  const stack = [];
  const captures = [];
  let text = "";
  for (const part of content.matchAll(
    /<span class='([^']*)'>|<[/]span>|([^<]+)/g,
  )) {
    if (part[1] !== undefined) stack.push(part[1].replaceAll(" ", "."));
    else if (part[0] === "</span>") assert.notEqual(stack.pop(), undefined);
    else {
      const decoded = decodeEntities(part[2]);
      text += decoded;
      captures.push(
        ...Array(Buffer.byteLength(decoded)).fill(stack.at(-1) ?? ""),
      );
    }
  }
  assert.equal(stack.length, 0, "unclosed highlight span");
  assert.equal(
    text.replace(/\n$/, ""),
    source.replace(/\n$/, ""),
    "rendered source differs from the input",
  );
  return captures;
}

function createHighlighter({ directory, root, run, captureNames }) {
  const parserDirectory = join(directory, "parsers");
  mkdirSync(parserDirectory);
  // CLI discovery requires a tree-sitter-* entry even when the checkout is renamed.
  symlinkSync(root, join(parserDirectory, "tree-sitter-test"), "junction");
  const configPath = join(directory, "highlight.json");
  const capturePath = join(directory, "captures.txt");
  writeFileSync(
    configPath,
    JSON.stringify({
      "parser-directories": [parserDirectory],
      theme: Object.fromEntries(
        captureNames.map((name, index) => [name, index + 17]),
      ),
    }),
  );
  writeFileSync(capturePath, `${captureNames.join("\n")}\n`);

  return (scope, source, valid = true) => {
    const path = join(directory, "highlight.txt");
    writeFileSync(path, source);
    if (valid) {
      const parsed = run(["parse", "--cst", "--scope", scope, path]);
      assert.doesNotMatch(parsed, /^[0-9: \t-]+•/m, parsed);
    }
    const captures = renderedCaptures(
      run([
        "highlight",
        "--check",
        "--captures-path",
        capturePath,
        "--config-path",
        configPath,
        "--html",
        "--layout",
        "fragment",
        "--style",
        "classes",
        "--scope",
        scope,
        path,
      ]),
      source,
    );
    for (const capture of captures) {
      assert.ok(
        capture === "" || captureNames.includes(capture),
        `unexpected final capture: ${capture}`,
      );
    }
    return captures;
  };
}

function assertCaptures(source, actual, ranges) {
  const bytes = Buffer.from(source);
  const expected = Array(bytes.length).fill("");
  let previousEnd = 0;
  for (const [start, end, capture] of ranges) {
    assert.ok(
      Number.isSafeInteger(start) && start >= previousEnd,
      "expected ranges must be ordered and disjoint",
    );
    assert.ok(
      Number.isSafeInteger(end) && end > start && end <= bytes.length,
      "expected range exceeds source bytes",
    );
    expected.fill(capture, start, end);
    previousEnd = end;
  }
  // HTML emits line breaks outside spans.
  for (const [index, byte] of bytes.entries()) {
    if (byte !== 10)
      assert.equal(
        actual[index],
        expected[index],
        `byte ${index} in ${JSON.stringify(source)}`,
      );
  }
}

const captureNames = [
  "character.special",
  "comment",
  "function",
  "function.builtin",
  "function.call",
  "keyword",
  "keyword.conditional.ternary",
  "number",
  "operator",
  "punctuation.bracket",
  "punctuation.delimiter",
  "punctuation.special",
  "string",
  "string.escape",
  "string.regexp",
  "variable",
  "variable.parameter",
];
let highlight;

let directory;
let runner;
before(() => {
  directory = mkdtempSync(join(tmpdir(), `${packageName}-highlight-`));
  runner = createTreeSitter();
  highlight = createHighlighter({
    directory,
    root,
    run: assertCommand,
    captureNames,
  });
});
after(() => {
  try {
    runner?.close();
  } finally {
    if (directory) rmSync(directory, { recursive: true, force: true });
  }
});

function assertCommand(arguments_) {
  const result = runner.run(arguments_, {
    timeout: 60_000,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.doesNotMatch(result.stderr, /Non-standard highlight captures/);
  return result.stdout;
}

const grammar = grammars[0];

const finalCaptureCases = [
  {
    name: "a continued function declaration distinguishes its name and parameter",
    source: "function foo\\\n(param) {return param}\n",
    captures: [
      [0, 8, "keyword"],
      [9, 12, "function"],
      [12, 13, "punctuation.special"],
      [14, 15, "punctuation.bracket"],
      [15, 20, "variable.parameter"],
      [20, 21, "punctuation.bracket"],
      [22, 23, "punctuation.bracket"],
      [23, 29, "keyword"],
      [30, 35, "variable"],
      [35, 36, "punctuation.bracket"],
    ],
  },
  {
    name: "a continuation separates a variable from an adjacent function call",
    source: "{len\\\ngth(x)}\n",
    captures: [
      [0, 1, "punctuation.bracket"],
      [1, 4, "variable"],
      [4, 5, "punctuation.special"],
      [6, 9, "function.call"],
      [9, 10, "punctuation.bracket"],
      [10, 11, "variable"],
      [11, 13, "punctuation.bracket"],
    ],
  },
  {
    name: "a continuation before a parenthesis leaves a variable capture",
    source: "{foo\\\n(x)}\n",
    captures: [
      [0, 1, "punctuation.bracket"],
      [1, 4, "variable"],
      [4, 5, "punctuation.special"],
      [6, 7, "punctuation.bracket"],
      [7, 8, "variable"],
      [8, 10, "punctuation.bracket"],
    ],
  },
  {
    name: "a continuation separates complete numeric tokens",
    source: "1.2e+3F\\\n4\n",
    captures: [
      [0, 7, "number"],
      [7, 8, "punctuation.special"],
      [9, 10, "number"],
    ],
  },
  {
    name: "complete logical operators leave continuations independently highlighted",
    source: "a||\\\nb&&\\\nc\n",
    captures: [
      [0, 1, "variable"],
      [1, 3, "operator"],
      [3, 4, "punctuation.special"],
      [5, 6, "variable"],
      [6, 8, "operator"],
      [8, 9, "punctuation.special"],
      [10, 11, "variable"],
    ],
  },
  {
    name: "complete Unicode strings retain distinct continuation captures",
    source: '"é"\\\n"😀"\n',
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 3, "string"],
      [3, 4, "punctuation.delimiter"],
      [4, 5, "punctuation.special"],
      [6, 7, "punctuation.delimiter"],
      [7, 11, "string"],
      [11, 12, "punctuation.delimiter"],
    ],
  },
  {
    name: "string and ERE escapes remain distinct from a continuation marker",
    source: '"\\n"\\\n&& /\\n/\n',
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 3, "string.escape"],
      [3, 4, "punctuation.delimiter"],
      [4, 5, "punctuation.special"],
      [6, 8, "operator"],
      [9, 10, "punctuation.delimiter"],
      [10, 12, "string.escape"],
      [12, 13, "punctuation.delimiter"],
    ],
  },
  {
    name: "string content retains spaces tabs and hashes",
    source: '"a #\tb"\n',
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 6, "string"],
      [6, 7, "punctuation.delimiter"],
    ],
  },
  {
    name: "an octal escape excludes following string content",
    source: '"\\123x"\n',
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 5, "string.escape"],
      [5, 6, "string"],
      [6, 7, "punctuation.delimiter"],
    ],
  },
  {
    name: "ERE class names and repetition counts preserve complete leaf captures",
    source: "/[[:alpha:]]a{12}/\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 3, "punctuation.bracket"],
      [3, 4, "punctuation.delimiter"],
      [4, 9, "character.special"],
      [9, 10, "punctuation.delimiter"],
      [10, 12, "punctuation.bracket"],
      [12, 13, "string.regexp"],
      [13, 14, "punctuation.bracket"],
      [14, 16, "number"],
      [16, 17, "punctuation.bracket"],
      [17, 18, "punctuation.delimiter"],
    ],
  },
  {
    name: "an escaped slash differs from the delimiter and following continuation",
    source: "/\\//\\\n&& /x/\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 3, "string.escape"],
      [3, 4, "punctuation.delimiter"],
      [4, 5, "punctuation.special"],
      [6, 8, "operator"],
      [9, 10, "punctuation.delimiter"],
      [10, 11, "string.regexp"],
      [11, 12, "punctuation.delimiter"],
    ],
  },
  {
    name: "HTML-sensitive Unicode literals retain source bytes and captures",
    source: `BEGIN { print "é😀<&>'" }\n`,
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 15, "punctuation.delimiter"],
      [15, 25, "string"],
      [25, 26, "punctuation.delimiter"],
      [27, 28, "punctuation.bracket"],
    ],
  },

  {
    name: "empty input has no captures",
    source: "",
    captures: [],
  },
  {
    name: "function declarations distinguish parameter lists from body references",
    source: "function add(left, right) {\n  return left + right\n}\n",
    captures: [
      [0, 8, "keyword"],
      [9, 12, "function"],
      [12, 13, "punctuation.bracket"],
      [13, 17, "variable.parameter"],
      [17, 18, "punctuation.delimiter"],
      [19, 24, "variable.parameter"],
      [24, 25, "punctuation.bracket"],
      [26, 27, "punctuation.bracket"],
      [30, 36, "keyword"],
      [37, 41, "variable"],
      [42, 43, "operator"],
      [44, 49, "variable"],
      [50, 51, "punctuation.bracket"],
    ],
  },
  {
    name: "nested user and builtin calls retain names, arguments and separators",
    source: "BEGIN {\n  value = add(atan2(1, 2), 3)\n}\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [10, 15, "variable"],
      [16, 17, "operator"],
      [18, 21, "function.call"],
      [21, 22, "punctuation.bracket"],
      [22, 27, "function.builtin"],
      [27, 28, "punctuation.bracket"],
      [28, 29, "number"],
      [29, 30, "punctuation.delimiter"],
      [31, 32, "number"],
      [32, 33, "punctuation.bracket"],
      [33, 34, "punctuation.delimiter"],
      [35, 36, "number"],
      [36, 37, "punctuation.bracket"],
      [38, 39, "punctuation.bracket"],
    ],
  },
  {
    name: "array subscripts and field references retain their lexical roles",
    source: "BEGIN { array[value] = $1 }\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "variable"],
      [13, 14, "punctuation.bracket"],
      [14, 19, "variable"],
      [19, 20, "punctuation.bracket"],
      [21, 22, "operator"],
      [23, 24, "operator"],
      [24, 25, "number"],
      [26, 27, "punctuation.bracket"],
    ],
  },
  {
    name: "assignment conditionals override generic operator captures",
    source: "BEGIN { result = value ? left : right }\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 14, "variable"],
      [15, 16, "operator"],
      [17, 22, "variable"],
      [23, 24, "keyword.conditional.ternary"],
      [25, 29, "variable"],
      [30, 31, "keyword.conditional.ternary"],
      [32, 37, "variable"],
      [38, 39, "punctuation.bracket"],
    ],
  },
  {
    name: "print argument lists and file redirections retain punctuation and strings",
    source: 'BEGIN { print value, text > "output" }\n',
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 19, "variable"],
      [19, 20, "punctuation.delimiter"],
      [21, 25, "variable"],
      [26, 27, "operator"],
      [28, 29, "punctuation.delimiter"],
      [29, 35, "string"],
      [35, 36, "punctuation.delimiter"],
      [37, 38, "punctuation.bracket"],
    ],
  },
  {
    name: "ERE anchors, groups and repetition operators retain their lexical roles",
    source: "/^(ab|c)+d{2,3}?e?f*$/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "operator"],
      [2, 3, "punctuation.bracket"],
      [3, 5, "string.regexp"],
      [5, 6, "operator"],
      [6, 7, "string.regexp"],
      [7, 8, "punctuation.bracket"],
      [8, 9, "operator"],
      [9, 10, "string.regexp"],
      [10, 11, "punctuation.bracket"],
      [11, 12, "number"],
      [12, 13, "punctuation.delimiter"],
      [13, 14, "number"],
      [14, 15, "punctuation.bracket"],
      [15, 16, "operator"],
      [16, 17, "string.regexp"],
      [17, 18, "operator"],
      [18, 19, "string.regexp"],
      [19, 21, "operator"],
      [21, 22, "punctuation.delimiter"],
      [23, 24, "punctuation.bracket"],
      [25, 30, "keyword"],
      [31, 32, "punctuation.bracket"],
    ],
  },
  {
    name: "ERE character escapes and escaped delimiters retain escape captures",
    source: "/a\\n\\t\\/b/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "string.regexp"],
      [2, 8, "string.escape"],
      [8, 9, "string.regexp"],
      [9, 10, "punctuation.delimiter"],
      [11, 12, "punctuation.bracket"],
      [13, 18, "keyword"],
      [19, 20, "punctuation.bracket"],
    ],
  },
  {
    name: "negated ERE brackets distinguish ranges, classes and collating elements",
    source: "/[^]a-c[:alpha:][.].][=a=]-]/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "punctuation.bracket"],
      [2, 3, "operator"],
      [3, 5, "character.special"],
      [5, 6, "operator"],
      [6, 7, "character.special"],
      [7, 8, "punctuation.bracket"],
      [8, 9, "punctuation.delimiter"],
      [9, 14, "character.special"],
      [14, 15, "punctuation.delimiter"],
      [15, 17, "punctuation.bracket"],
      [17, 18, "punctuation.delimiter"],
      [18, 19, "character.special"],
      [19, 20, "punctuation.delimiter"],
      [20, 22, "punctuation.bracket"],
      [22, 23, "punctuation.delimiter"],
      [23, 24, "character.special"],
      [24, 25, "punctuation.delimiter"],
      [25, 26, "punctuation.bracket"],
      [26, 27, "string.regexp"],
      [27, 28, "punctuation.bracket"],
      [28, 29, "punctuation.delimiter"],
      [30, 31, "punctuation.bracket"],
      [32, 37, "keyword"],
      [38, 39, "punctuation.bracket"],
    ],
  },
  {
    name: "a range-ending hyphen remains literal before the closing bracket",
    source: "/[%--]/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "punctuation.bracket"],
      [2, 3, "character.special"],
      [3, 4, "operator"],
      [4, 5, "string.regexp"],
      [5, 6, "punctuation.bracket"],
      [6, 7, "punctuation.delimiter"],
      [8, 9, "punctuation.bracket"],
      [10, 15, "keyword"],
      [16, 17, "punctuation.bracket"],
    ],
  },
  {
    name: "spaced function declarations override call captures and classify parameters",
    source: "function spaced (first) { return first }\n",
    captures: [
      [0, 8, "keyword"],
      [9, 15, "function"],
      [16, 17, "punctuation.bracket"],
      [17, 22, "variable.parameter"],
      [22, 23, "punctuation.bracket"],
      [24, 25, "punctuation.bracket"],
      [26, 32, "keyword"],
      [33, 38, "variable"],
      [39, 40, "punctuation.bracket"],
    ],
  },
  {
    name: "ERE literal closers and escaped slashes retain their lexical captures",
    source: "BEGIN { print /a)b}c\\/d/ }\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 15, "punctuation.delimiter"],
      [15, 20, "string.regexp"],
      [20, 22, "string.escape"],
      [22, 23, "string.regexp"],
      [23, 24, "punctuation.delimiter"],
      [25, 26, "punctuation.bracket"],
    ],
  },
  {
    name: "equivalence classes separate brackets, markers and elements",
    source: "/[x[=a=]]/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "punctuation.bracket"],
      [2, 3, "character.special"],
      [3, 4, "punctuation.bracket"],
      [4, 5, "punctuation.delimiter"],
      [5, 6, "character.special"],
      [6, 7, "punctuation.delimiter"],
      [7, 9, "punctuation.bracket"],
      [9, 10, "punctuation.delimiter"],
      [11, 12, "punctuation.bracket"],
      [13, 18, "keyword"],
      [19, 20, "punctuation.bracket"],
    ],
  },
  {
    name: "conditional punctuation overrides generic operator captures",
    source: "END { print first ? 1 : 2 }\n",
    captures: [
      [0, 3, "keyword"],
      [4, 5, "punctuation.bracket"],
      [6, 11, "keyword"],
      [12, 17, "variable"],
      [18, 19, "keyword.conditional.ternary"],
      [20, 21, "number"],
      [22, 23, "keyword.conditional.ternary"],
      [24, 25, "number"],
      [26, 27, "punctuation.bracket"],
    ],
  },
  {
    name: "a range-ending hyphen remains literal before the next bracket member",
    source: "/[%--@]/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "punctuation.bracket"],
      [2, 3, "character.special"],
      [3, 4, "operator"],
      [4, 5, "string.regexp"],
      [5, 6, "character.special"],
      [6, 7, "punctuation.bracket"],
      [7, 8, "punctuation.delimiter"],
      [9, 10, "punctuation.bracket"],
      [11, 16, "keyword"],
      [17, 18, "punctuation.bracket"],
    ],
  },
  {
    name: "a lone bracket hyphen keeps its literal capture",
    source: "/[-]/ { print }\n",
    captures: [
      [0, 1, "punctuation.delimiter"],
      [1, 2, "punctuation.bracket"],
      [2, 3, "string.regexp"],
      [3, 4, "punctuation.bracket"],
      [4, 5, "punctuation.delimiter"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 15, "punctuation.bracket"],
    ],
  },
  {
    name: "UTF-8 string contents and escapes retain byte ranges",
    source: 'BEGIN { print "é😀\\n" }\n',
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 15, "punctuation.delimiter"],
      [15, 21, "string"],
      [21, 23, "string.escape"],
      [23, 24, "punctuation.delimiter"],
      [25, 26, "punctuation.bracket"],
    ],
  },
  {
    name: "builtin calls retain variable, operator and bracket captures",
    source: "BEGIN { total += length(value) }\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "variable"],
      [14, 16, "operator"],
      [17, 23, "function.builtin"],
      [23, 24, "punctuation.bracket"],
      [24, 29, "variable"],
      [29, 30, "punctuation.bracket"],
      [31, 32, "punctuation.bracket"],
    ],
  },
  {
    name: "continuations and UTF-8 comments retain their source ranges",
    source: "BEGIN { print \\\n  1 # é😀\n}\n",
    captures: [
      [0, 5, "keyword"],
      [6, 7, "punctuation.bracket"],
      [8, 13, "keyword"],
      [14, 15, "punctuation.special"],
      [18, 19, "number"],
      [20, 28, "comment"],
      [29, 30, "punctuation.bracket"],
    ],
  },
];

for (const { name, source, captures } of finalCaptureCases) {
  test(`${grammar.name}: ${name}`, () => {
    assertCaptures(source, highlight(grammar.scope, source), captures);
  });
}

for (const [name, source] of [
  ["collating symbols", `${String.raw`/[[.a\né\141 \e\/日\..]]/`}\n`],
  ["equivalence classes", `${String.raw`/[[=a\né\141 \e\/日\.=]]/`}\n`],
]) {
  test(`${grammar.name}: ${name} separate Unicode and blank content from escapes`, () => {
    assertCaptures(source, highlight(grammar.scope, source), [
      [0, 1, "punctuation.delimiter"],
      [1, 3, "punctuation.bracket"],
      [3, 4, "punctuation.delimiter"],
      [4, 5, "character.special"],
      [5, 7, "string.escape"],
      [7, 9, "character.special"],
      [9, 13, "string.escape"],
      [13, 14, "character.special"],
      [14, 18, "string.escape"],
      [18, 21, "character.special"],
      [21, 23, "string.escape"],
      [23, 24, "punctuation.delimiter"],
      [24, 26, "punctuation.bracket"],
      [26, 27, "punctuation.delimiter"],
    ]);
  });
}

test(`${grammar.name}: incomplete input preserves source without error colors`, () => {
  for (const source of ["BEGIN {", 'BEGIN { print "', "BEGIN { print /[a"]) {
    highlight(grammar.scope, source, false);
  }
});
