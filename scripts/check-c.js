const childProcess = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const {
  grammarDirectory,
  repositoryDirectory,
  throwIfFailed,
} = require("./tree-sitter.js");
const cFiles = [
  path.join(grammarDirectory, "src", "scanner.c"),
  path.join(repositoryDirectory, "test", "scanner.test.c"),
];

function run(command, args) {
  const result = childProcess.spawnSync(command, args, {
    cwd: repositoryDirectory,
    stdio: "inherit",
  });
  throwIfFailed(result, command);
}

let llvmPrefix;

function llvmTool(name, environmentName) {
  if (process.platform !== "darwin") {
    return process.env[environmentName] ?? name;
  }
  if (llvmPrefix === undefined) {
    const result = childProcess.spawnSync("brew", ["--prefix", "llvm"], {
      encoding: "utf8",
    });
    throwIfFailed(result, "brew --prefix llvm");
    llvmPrefix = result.stdout.trim();
  }
  return path.join(llvmPrefix, "bin", name);
}

if (process.argv.length === 3 && process.argv[2] === "--write") {
  run(llvmTool("clang-format", "CLANG_FORMAT"), ["-i", ...cFiles]);
} else if (process.argv.length === 2) {
  run(llvmTool("clang-format", "CLANG_FORMAT"), [
    "--dry-run",
    "--Werror",
    ...cFiles,
  ]);

  const testDirectory = fs.mkdtempSync(
    path.join(os.tmpdir(), "tree-sitter-posix-awk-c."),
  );
  try {
    const clang = llvmTool("clang", "CLANG");
    const scannerInclude = path.join(grammarDirectory, "src");
    const compileCommands = cFiles.map((cFile) => ({
      arguments: [
        clang,
        "-std=c17",
        `-I${scannerInclude}`,
        "-fsyntax-only",
        cFile,
      ],
      directory: repositoryDirectory,
      file: cFile,
    }));
    fs.writeFileSync(
      path.join(testDirectory, "compile_commands.json"),
      `${JSON.stringify(compileCommands)}\n`,
    );

    const clangd = llvmTool("clangd", "CLANGD");
    for (const cFile of cFiles) {
      run(clangd, [
        `--compile-commands-dir=${testDirectory}`,
        `--check=${cFile}`,
        "--tweaks=",
      ]);
    }

    for (const standard of ["c99", "c17"]) {
      const testBinary = path.join(testDirectory, `scanner-${standard}`);
      run(clang, [
        `-std=${standard}`,
        "-Wall",
        "-Wextra",
        "-Werror",
        "-pedantic",
        "-I",
        scannerInclude,
        path.join(repositoryDirectory, "test", "scanner.test.c"),
        "-o",
        testBinary,
      ]);
      run(testBinary, []);
    }
  } finally {
    fs.rmSync(testDirectory, { force: true, recursive: true });
  }
} else {
  throw new Error("Usage: node scripts/check-c.js [--write]");
}
