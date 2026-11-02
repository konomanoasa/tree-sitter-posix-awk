import assert from "node:assert/strict";
import { test } from "node:test";
import AWK from "@konomanoasa/tree-sitter-posix-awk";
import Parser from "tree-sitter";

test("Node.js language parses POSIX awk with the scanner and exposes node metadata", () => {
  const parser = new Parser();
  parser.setLanguage(AWK);
  const source = "/a/ { print $1 }\n";
  const tree = parser.parse(source);
  assert.equal(tree.rootNode.type, "program");
  assert.equal(tree.rootNode.hasError, false);
  assert.equal(tree.rootNode.text, source);
  assert.ok(
    AWK.nodeTypeInfo.some((node) => node.type === "program" && node.named),
  );
});

test("Node.js packaged highlights capture POSIX awk keywords", () => {
  const parser = new Parser();
  parser.setLanguage(AWK);
  const tree = parser.parse("/a/ { print $1 }\n");
  const query = new Parser.Query(AWK, AWK.HIGHLIGHTS_QUERY);
  assert.deepEqual(
    query
      .captures(tree.rootNode)
      .filter(({ name }) => name === "keyword")
      .map(({ name, node }) => [name, node.text]),
    [["keyword", "print"]],
  );
});
