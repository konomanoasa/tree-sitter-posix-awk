# tree-sitter-posix-awk

[![CI](https://github.com/konomanoasa/tree-sitter-posix-awk/actions/workflows/ci.yaml/badge.svg)](https://github.com/konomanoasa/tree-sitter-posix-awk/actions/workflows/ci.yaml)
[![npm](https://img.shields.io/npm/v/@konomanoasa/tree-sitter-posix-awk)](https://www.npmjs.com/package/@konomanoasa/tree-sitter-posix-awk)

[Tree-sitter](https://tree-sitter.github.io/tree-sitter/) grammar for
POSIX.1-2024 AWK.

## Installation

```sh
npm install @konomanoasa/tree-sitter-posix-awk
```

## Grammar

| Grammar | Description | Rust constant |
| --- | --- | --- |
| `posix_awk` | POSIX.1-2024 AWK | `LANGUAGE` |

## Development

Development requires Node.js 24.2.0 or later.

```sh
npm install
npm run build
npm test
```

## Specifications

- [POSIX.1-2024 AWK](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/utilities/awk.html)
- [POSIX.1-2024 extended regular expressions](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/basedefs/V1_chap09.html#tag_09_04)

## License

[MIT](LICENSE)
