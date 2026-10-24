export const getlineSelectionWorkloads = [
  {
    name: "getline source chain ending in a comparison/64",
    source: `{ ${"getline $getline $getline < ".repeat(64)}y < x++++ }\n`,
    successful: true,
  },
  {
    name: "getline source chain ending in two comparisons/24",
    source: `{ ${"getline $getline < ".repeat(24)}y < z < x++++ }\n`,
    successful: true,
  },
];

export function* normalWorkloads() {
  yield* getlineSelectionWorkloads;
  const depths = [16, 32, 64];
  for (const expression of [
    "$getline $-getline < 1 ++ ",
    "$getline $-getline < 1 ",
  ]) {
    for (const count of depths)
      yield {
        name: `nested getline/${expression}/${count}`,
        source: `{ ${expression.repeat(count)}}\n`,
        successful: true,
      };
  }
  for (const unit of ["getline $getline < ", "getline $getline $getline < "]) {
    for (const count of depths) {
      for (const ending of ["", "x y = 1", "x in"]) {
        yield {
          name: `incomplete getline/${unit}/${count}/${ending}`,
          source: `{ ${unit.repeat(count)}${ending} }\n`,
          successful: false,
        };
      }
    }
  }
}

export const scalingWorkloads = [
  {
    name: "long strings",
    sizes: [5000, 20000, 80000],
    source: (n) => `BEGIN { print "${"x".repeat(n)}" }\n`,
  },
  {
    name: "statement lists",
    sizes: [1000, 4000, 16000],
    source: (n) => `BEGIN { ${"x++;".repeat(n)} }\n`,
  },
];
