const childProcess = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const repositoryDirectory = path.resolve(__dirname, "..");
const manifest = JSON.parse(
  fs.readFileSync(path.join(repositoryDirectory, "tree-sitter.json"), "utf8"),
);

if (!Array.isArray(manifest.grammars) || manifest.grammars.length !== 1) {
  throw new Error("tree-sitter.json must declare exactly one grammar");
}

const grammar = manifest.grammars[0];
const grammarPath = grammar?.path ?? ".";
if (
  grammar === null ||
  typeof grammar !== "object" ||
  typeof grammar.name !== "string" ||
  grammar.name.length === 0 ||
  typeof grammar.scope !== "string" ||
  grammar.scope.length === 0 ||
  typeof grammarPath !== "string" ||
  grammarPath.length === 0
) {
  throw new Error("tree-sitter.json grammar metadata is incomplete");
}

const grammarDirectory = path.resolve(repositoryDirectory, grammarPath);
const treeSitterCli = require.resolve("tree-sitter-cli/cli.js");

function createEnvironment(prefix = "tree-sitter-posix-awk.") {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  const cacheDirectory = path.join(
    repositoryDirectory,
    "node_modules",
    ".cache",
    "tree-sitter-posix-awk",
  );
  const configDirectory = path.join(directory, "config");
  const libraryDirectory = path.join(directory, "lib");
  const treeSitterConfigDirectory = path.join(configDirectory, "tree-sitter");
  const configPath = path.join(treeSitterConfigDirectory, "config.json");
  try {
    fs.mkdirSync(cacheDirectory, { recursive: true });
    fs.mkdirSync(treeSitterConfigDirectory, { recursive: true });
    fs.mkdirSync(libraryDirectory);
    fs.writeFileSync(
      configPath,
      `${JSON.stringify(
        { "parser-directories": [path.dirname(grammarDirectory)] },
        null,
        2,
      )}\n`,
    );
  } catch (error) {
    fs.rmSync(directory, { force: true, recursive: true });
    throw error;
  }

  return {
    directory,
    environment: {
      ...process.env,
      APPDATA: configDirectory,
      LOCALAPPDATA: cacheDirectory,
      NO_COLOR: "1",
      TREE_SITTER_DIR: treeSitterConfigDirectory,
      TREE_SITTER_LIBDIR: libraryDirectory,
      TREE_SITTER_SEED: process.env.TREE_SITTER_SEED ?? "1",
      XDG_CACHE_HOME: cacheDirectory,
      XDG_CONFIG_HOME: configDirectory,
    },
    libraryDirectory,
    remove() {
      fs.rmSync(directory, { force: true, recursive: true });
    },
  };
}

function run(args, options = {}) {
  const environment = options.environment ?? createEnvironment();
  const ownsEnvironment = options.environment === undefined;
  try {
    return childProcess.spawnSync(process.execPath, [treeSitterCli, ...args], {
      cwd: repositoryDirectory,
      encoding: options.encoding ?? "utf8",
      env: environment.environment,
      stdio: options.stdio ?? "pipe",
    });
  } finally {
    if (ownsEnvironment) {
      environment.remove();
    }
  }
}

function throwIfFailed(result, description) {
  if (result.error !== undefined) {
    throw result.error;
  }
  if (result.status !== 0) {
    if (result.stdout) {
      process.stderr.write(result.stdout);
    }
    if (result.stderr) {
      process.stderr.write(result.stderr);
    }
    throw new Error(`${description} exited with status ${result.status}`);
  }
}

function runChecked(args, options = {}) {
  const result = run(args, options);
  throwIfFailed(result, `tree-sitter ${args.join(" ")}`);
  return result;
}

module.exports = {
  createEnvironment,
  grammar,
  grammarDirectory,
  repositoryDirectory,
  run,
  runChecked,
  throwIfFailed,
};

if (require.main === module) {
  const result = run(process.argv.slice(2), {
    encoding: null,
    stdio: "inherit",
  });
  if (result.error !== undefined) {
    throw result.error;
  }
  process.exitCode = result.status ?? 1;
}
