const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const { grammarDirectory, runChecked } = require("./tree-sitter.js");

const expectedGeneratedFiles = [
  "grammar.json",
  "node-types.json",
  "parser.c",
  "tree_sitter/alloc.h",
  "tree_sitter/array.h",
  "tree_sitter/parser.h",
];

const budgets = {
  STATE_COUNT: 18963,
  LARGE_STATE_COUNT: 4488,
  SYMBOL_COUNT: 538,
  EXTERNAL_TOKEN_COUNT: 98,
  parser_bytes: 37842229,
  maximum_ACTIONS_index: 8804,
  parse_table_storage_bytes: 6140066,
};

function listFiles(directory, prefix = "") {
  const files = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const relativePath = path.posix.join(prefix, entry.name);
    if (entry.isDirectory()) {
      files.push(...listFiles(path.join(directory, entry.name), relativePath));
    } else if (entry.isFile()) {
      files.push(relativePath);
    }
  }
  return files.sort();
}

function readDefine(parser, name) {
  const match = parser.match(new RegExp(`^#define ${name} ([0-9]+)$`, "m"));
  if (match === null) {
    throw new Error(`src/parser.c does not define ${name} as an integer`);
  }
  return Number(match[1]);
}

function maximumActionIndex(parser) {
  let maximum;
  for (const match of parser.matchAll(/ACTIONS\(([0-9]+)\)/g)) {
    const value = Number(match[1]);
    maximum = maximum === undefined ? value : Math.max(maximum, value);
  }
  if (maximum === undefined) {
    throw new Error("src/parser.c contains no ACTIONS index");
  }
  return maximum;
}

function smallParseTableWordCount(parser) {
  const declaration = "static const uint16_t ts_small_parse_table[] = {\n";
  const start = parser.indexOf(declaration);
  if (start === -1) {
    throw new Error("src/parser.c contains no small parse table");
  }
  const initializerStart = start + declaration.length;
  const initializerEnd = parser.indexOf("\n};", initializerStart);
  if (initializerEnd === -1) {
    throw new Error("src/parser.c contains an unterminated small parse table");
  }
  const initializer = parser.slice(initializerStart, initializerEnd);

  let finalIndex;
  let finalOffset;
  for (const match of initializer.matchAll(/^ {2}\[([0-9]+)\] =/gm)) {
    finalIndex = Number(match[1]);
    finalOffset = match.index;
  }
  if (finalIndex === undefined || finalOffset === undefined) {
    throw new Error("src/parser.c small parse table has no indexed row");
  }

  const finalRow = initializer.slice(finalOffset);
  const finalRowWordCount = finalRow.match(/,/g)?.length ?? 0;
  if (finalRowWordCount === 0) {
    throw new Error("src/parser.c small parse table has an empty final row");
  }
  return finalIndex + finalRowWordCount;
}

function parseTableStorageBytes(parser, actual) {
  const smallStateCount = actual.STATE_COUNT - actual.LARGE_STATE_COUNT;
  if (smallStateCount < 0) {
    throw new Error("src/parser.c has more large states than total states");
  }
  return (
    actual.LARGE_STATE_COUNT * actual.SYMBOL_COUNT * 2 +
    smallParseTableWordCount(parser) * 2 +
    smallStateCount * 4
  );
}

function checkGenerated() {
  const generationDirectory = fs.mkdtempSync(
    path.join(os.tmpdir(), "tree-sitter-posix-awk-generated."),
  );
  let failed = false;
  try {
    runChecked(
      [
        "generate",
        "--output",
        generationDirectory,
        path.join(grammarDirectory, "grammar.js"),
      ],
      { stdio: "inherit" },
    );

    const actualGeneratedFiles = listFiles(generationDirectory);
    if (
      JSON.stringify(actualGeneratedFiles) !==
      JSON.stringify(expectedGeneratedFiles)
    ) {
      console.error("Generated file manifest differs.");
      console.error(`Expected:\n${expectedGeneratedFiles.join("\n")}`);
      console.error(`Actual:\n${actualGeneratedFiles.join("\n")}`);
      failed = true;
    }

    for (const generatedFile of expectedGeneratedFiles) {
      const generatedPath = path.join(generationDirectory, generatedFile);
      const checkedInPath = path.join(grammarDirectory, "src", generatedFile);
      if (
        !fs.existsSync(generatedPath) ||
        !fs.existsSync(checkedInPath) ||
        !fs.readFileSync(generatedPath).equals(fs.readFileSync(checkedInPath))
      ) {
        console.error(`Generated file is stale: src/${generatedFile}`);
        failed = true;
      }
    }
  } finally {
    fs.rmSync(generationDirectory, { force: true, recursive: true });
  }

  return failed;
}

function checkParserBudget() {
  const parserPath = path.join(grammarDirectory, "src", "parser.c");
  const parser = fs.readFileSync(parserPath, "utf8");
  const actual = {
    STATE_COUNT: readDefine(parser, "STATE_COUNT"),
    LARGE_STATE_COUNT: readDefine(parser, "LARGE_STATE_COUNT"),
    SYMBOL_COUNT: readDefine(parser, "SYMBOL_COUNT"),
    EXTERNAL_TOKEN_COUNT: readDefine(parser, "EXTERNAL_TOKEN_COUNT"),
    parser_bytes: fs.statSync(parserPath).size,
    maximum_ACTIONS_index: maximumActionIndex(parser),
  };
  actual.parse_table_storage_bytes = parseTableStorageBytes(parser, actual);

  console.log("Metric                     Actual      Maximum");
  let failed = false;
  for (const [name, maximum] of Object.entries(budgets)) {
    console.log(
      `${name.padEnd(22)} ${String(actual[name]).padStart(12)} ${String(maximum).padStart(12)}`,
    );
    if (actual[name] > maximum) {
      console.error(
        `${name} exceeds its parser budget: ${actual[name]} > ${maximum}`,
      );
      failed = true;
    }
  }
  return failed;
}

const generatedFailed = checkGenerated();
const budgetFailed = checkParserBudget();
if (generatedFailed || budgetFailed) {
  process.exitCode = 1;
}
