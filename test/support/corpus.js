import assert from "node:assert/strict";

function parseCorpus(text) {
  const sections = text.split("==================\n");
  assert.equal(sections.shift(), "", "corpus must start with a case header");
  assert.equal(sections.length % 2, 0, "corpus must pair headers and bodies");
  const cases = [];
  for (let index = 0; index < sections.length; index += 2) {
    const [name, ...attributes] = sections[index].trimEnd().split("\n");
    assert.ok(name, "corpus case must have a name");
    for (const attribute of attributes) {
      assert.ok(attribute === ":cst" || attribute === ":error", attribute);
    }
    const parts = sections[index + 1].split("\n------------------\n");
    assert.equal(
      parts.length,
      2,
      `${name}: corpus must separate source and expectation`,
    );
    const source = parts[0].endsWith("\r") ? parts[0].slice(0, -1) : parts[0];
    cases.push({ name, source });
  }
  return cases;
}

export { parseCorpus };
