#pragma once

#include <string>
#include <vector>
#include <array>

class BPlusTree {
public:
    static const int PAGE_SIZE  = 4096;
    static const int TUPLE_SIZE = 100;

    //Disk structures
    struct FileHeader;
    struct LeafNodePage;
    struct InternalNodePage;

    // Open or create index file
    explicit BPlusTree(const std::string& filename);
    ~BPlusTree();

    //Public APIs required by assignment

    // Insert or update key -> 100-byte tuple
    bool writeData(int key, const unsigned char* data);

    // Delete key if exists (no rebalancing)
    bool deleteData(int key);

    // Read tuple for key; returns pointer to internal static buffer
    // Copy it out if you need to keep it.
    const unsigned char* readData(int key);

    // Range read: returns vector of 100-byte tuples with keys in [lowerKey, upperKey]
    std::vector<std::array<unsigned char, TUPLE_SIZE>>
    readRangeData(int lowerKey, int upperKey);

private:
    std::string filename;
    int fd;
    bool isNewFile;

    //  Low-level I/O 

    FileHeader loadHeader();
    void       storeHeader(const FileHeader& hdr);

    void readPage(int pageId, void* buffer);
    void writePage(int pageId, const void* buffer);

    int  allocatePage();
    void initEmptyTree();

    // Tree navigation & operations 

    int  findLeafPageForKey(int key);
    const unsigned char* searchKey(int key);

    bool insertRecursive(int pageId, int key, const unsigned char* data,
                         int& newChildPage, int& newChildKey);

    bool insertInLeaf(int leafPageId, int key, const unsigned char* data,
                      int& newChildPage, int& newChildKey);

    bool removeFromLeaf(int leafPageId, int key);
};
