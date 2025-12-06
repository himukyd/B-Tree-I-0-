# Disk-based B+ Tree Index

This project implements a simple disk-backed B+ Tree index with the following properties:

- **Page size**: 4096 bytes
- **Key type**: `int`
- **Value/Tuple size**: fixed 100-byte record
- **Storage**: single file (`index.dat`), page-structured
- **Nodes**:
  - Leaf nodes: store (key, value) pairs and are linked via `nextLeafPageId`
  - Internal nodes: store keys and child page IDs
- **APIs** (as member functions of `BPlusTree`):
  - `bool writeData(int key, const unsigned char* data);`
  - `bool deleteData(int key);`
  - `const unsigned char* readData(int key);`
  - `std::vector<std::array<unsigned char, TUPLE_SIZE>> readRangeData(int lowerKey, int upperKey);`

> If you need plain C-style functions (e.g., `unsigned char** readRangeData(...)`),
> you can add thin wrappers that call these methods.

## File Layout

- **Page 0**: File header (`FileHeader`)
  - Magic bytes `"BPTREE"`
  - Root page ID
  - Next free page ID
- **Page 1**: Initial empty root leaf node
- **Pages 2+**: Internal or leaf nodes

Each page has fixed size 4096 bytes.

## Build Instructions

Make sure you have a C++17-compatible compiler (e.g., `g++`).

```bash
make
