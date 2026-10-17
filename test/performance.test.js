import assert from "node:assert/strict";
import { test } from "node:test";
import { parseSummary } from "./support/parser.js";
import { normalWorkloads, scalingWorkloads } from "./support/workloads.js";

const warmupSamples = 1;
const measurementSamples = 3;
const normalDurationLimitMs = 1000;
const scalingGrowthAllowance = 3;
const timingNoiseAllowanceMs = 10;

function measure(source, successful = true) {
  const durations = [];
  for (let run = 0; run < warmupSamples + measurementSamples; run++) {
    const summary = parseSummary(source);
    assert.equal(summary.successful, successful);
    if (run >= warmupSamples)
      durations.push(
        summary.duration.secs * 1000 + summary.duration.nanos / 1e6,
      );
  }
  return durations.toSorted((a, b) => a - b)[Math.floor(durations.length / 2)];
}
for (const { name, source, successful } of normalWorkloads()) {
  test(`posix_awk: ${name} stays within the ordinary editing budget`, (context) => {
    const duration = measure(source, successful);
    context.diagnostic(`${name}: ${duration}ms`);
    assert.ok(duration < normalDurationLimitMs, name);
  });
}
for (const { name, sizes, source } of scalingWorkloads) {
  test(`posix_awk: ${name} avoid extreme scaling`, (context) => {
    let previous;
    for (const size of sizes) {
      const contents = source(size);
      const current = {
        duration: measure(contents),
        bytes: Buffer.byteLength(contents),
      };
      context.diagnostic(`${name}/${size}: ${current.duration}ms`);
      if (previous) {
        assert.ok(
          current.duration <=
            ((previous.duration * current.bytes) / previous.bytes) *
              scalingGrowthAllowance +
              timingNoiseAllowanceMs,
          name,
        );
      }
      previous = current;
    }
  });
}
