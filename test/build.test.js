import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import {
  cpSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { root } from "../scripts/tree-sitter.js";

test("posix_awk: Rust rebuilds the grammar after parser or allocator header changes", () => {
  const directory = mkdtempSync(join(tmpdir(), "tree-sitter-posix-awk-build-"));
  const source = join(directory, "source");
  try {
    for (const path of [
      "Cargo.toml",
      "Cargo.lock",
      "bindings/rust",
      "queries",
      "src",
    ]) {
      const destination = join(source, path);
      mkdirSync(dirname(destination), { recursive: true });
      cpSync(join(root, path), destination, { recursive: true });
    }

    function check() {
      const result = spawnSync(
        "cargo",
        [
          "check",
          "--locked",
          "--lib",
          "--manifest-path",
          join(source, "Cargo.toml"),
          "--target-dir",
          join(directory, "target"),
        ],
        { encoding: "utf8" },
      );
      if (result.error) throw result.error;
      return { status: result.status, output: result.stdout + result.stderr };
    }

    const initial = check();
    assert.equal(initial.status, 0, initial.output);
    for (const header of ["parser.h", "alloc.h"]) {
      const path = join(source, "src", "tree_sitter", header);
      const original = readFileSync(path);
      const marker = "tree_sitter_posix_awk_header_change_requires_rebuild";
      writeFileSync(path, `${original}\n#error ${marker}\n`);
      const changed = check();
      assert.notEqual(changed.status, 0, `${path}: ${changed.output}`);
      assert.ok(changed.output.includes(marker), changed.output);
      writeFileSync(path, original);
      const restored = check();
      assert.equal(restored.status, 0, restored.output);
    }
  } finally {
    rmSync(directory, { recursive: true, force: true });
  }
});
