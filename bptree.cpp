#include "bptree.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdexcept>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <cstdint>

//On-disk header

struct BPlusTree::FileHeader {
    char magic[8];      
    int  rootPageId;
    int  nextFreePageId; 
};

#pragma pack(push, 1)

//Leaf node page 

struct BPlusTree::LeafNodePage {
    uint8_t isLeaf;      
    int32_t numKeys;
    int32_t parentPageId;   
    int32_t nextLeafPageId; 

    static const int MAX_KEYS = 39;  

    int32_t keys[MAX_KEYS];
    unsigned char values[MAX_KEYS][TUPLE_SIZE];
};

// Internal node page 

struct BPlusTree::InternalNodePage {
    uint8_t isLeaf;      
    int32_t numKeys;
    int32_t parentPageId;  

    // small but safe branching factor
    static const int MAX_KEYS = 255;

    int32_t keys[MAX_KEYS];
    int32_t children[MAX_KEYS + 1];
};

#pragma pack(pop)

// Ensure we fit into pages
static_assert(sizeof(BPlusTree::LeafNodePage)     <= BPlusTree::PAGE_SIZE,
              "LeafNodePage too big");
static_assert(sizeof(BPlusTree::InternalNodePage) <= BPlusTree::PAGE_SIZE,
              "InternalNodePage too big");

//  Constructor / Destructor 

BPlusTree::BPlusTree(const std::string& filename)
    : filename(filename), fd(-1), isNewFile(false)
{
    fd = ::open(filename.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        throw std::runtime_error("Failed to open index file");
    }

    off_t size = ::lseek(fd, 0, SEEK_END);
    if (size == 0) {
        isNewFile = true;
        initEmptyTree();
    }
}

BPlusTree::~BPlusTree() {
    if (fd >= 0) {
        ::close(fd);
    }
}

// Low-level page I/O

void BPlusTree::readPage(int pageId, void* buffer) {
    off_t offset = static_cast<off_t>(pageId) * PAGE_SIZE;
    if (::lseek(fd, offset, SEEK_SET) < 0) {
        throw std::runtime_error("lseek failed in readPage");
    }
    ssize_t n = ::read(fd, buffer, PAGE_SIZE);
    if (n != PAGE_SIZE) {
        throw std::runtime_error("short read in readPage");
    }
}

void BPlusTree::writePage(int pageId, const void* buffer) {
    off_t offset = static_cast<off_t>(pageId) * PAGE_SIZE;
    if (::lseek(fd, offset, SEEK_SET) < 0) {
        throw std::runtime_error("lseek failed in writePage");
    }
    ssize_t n = ::write(fd, buffer, PAGE_SIZE);
    if (n != PAGE_SIZE) {
        throw std::runtime_error("short write in writePage");
    }
}

BPlusTree::FileHeader BPlusTree::loadHeader() {
    FileHeader hdr{};
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        throw std::runtime_error("lseek failed in loadHeader");
    }
    ssize_t n = ::read(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr)) {
        throw std::runtime_error("Failed to read header");
    }
    // basic magic check (optional)
    if (std::memcmp(hdr.magic, "BPTREE", 6) != 0) {
        throw std::runtime_error("Invalid index file (bad magic)");
    }
    return hdr;
}

void BPlusTree::storeHeader(const FileHeader& hdr) {
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        throw std::runtime_error("lseek failed in storeHeader");
    }
    ssize_t n = ::write(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr)) {
        throw std::runtime_error("Failed to write header");
    }
    // zero pad remaining bytes in header page if first time
    if (sizeof(hdr) < PAGE_SIZE) {
        static char zeros[PAGE_SIZE - sizeof(hdr)] = {0};
        ::write(fd, zeros, sizeof(zeros));
    }
}

int BPlusTree::allocatePage() {
    FileHeader hdr = loadHeader();
    int newPageId = hdr.nextFreePageId++;
    storeHeader(hdr);

    // zero the page
    char buf[PAGE_SIZE];
    std::memset(buf, 0, sizeof(buf));
    writePage(newPageId, buf);

    return newPageId;
}

void BPlusTree::initEmptyTree() {
    FileHeader hdr{};
    std::memset(hdr.magic, 0, sizeof(hdr.magic));
    std::memcpy(hdr.magic, "BPTREE", 6);
    hdr.rootPageId     = 1;
    hdr.nextFreePageId = 2;  

    // write header page
    storeHeader(hdr);

    // create empty root leaf at page 1
    LeafNodePage leaf{};
    leaf.isLeaf         = 1;
    leaf.numKeys        = 0;
    leaf.parentPageId   = -1;
    leaf.nextLeafPageId = -1;

    char page[PAGE_SIZE];
    std::memset(page, 0, sizeof(page));
    std::memcpy(page, &leaf, sizeof(leaf));
    writePage(1, page);
}

//Tree navigation

int BPlusTree::findLeafPageForKey(int key) {
    FileHeader hdr = loadHeader();
    int pageId = hdr.rootPageId;

    while (true) {
        char page[PAGE_SIZE];
        readPage(pageId, page);

        uint8_t isLeaf = *reinterpret_cast<uint8_t*>(page);
        if (isLeaf) {
            return pageId;
        } else {
            InternalNodePage node{};
            std::memcpy(&node, page, sizeof(node));

            int i = 0;
            while (i < node.numKeys && key >= node.keys[i]) {
                ++i;
            }
            pageId = node.children[i];
        }
    }
}

const unsigned char* BPlusTree::searchKey(int key) {
    int leafPageId = findLeafPageForKey(key);

    char page[PAGE_SIZE];
    readPage(leafPageId, page);

    LeafNodePage leaf{};
    std::memcpy(&leaf, page, sizeof(leaf));

    for (int i = 0; i < leaf.numKeys; ++i) {
        if (leaf.keys[i] == key) {
            static unsigned char result[TUPLE_SIZE];
            std::memcpy(result, leaf.values[i], TUPLE_SIZE);
            return result;
        }
    }
    return nullptr;
}

// Public read APIs 

const unsigned char* BPlusTree::readData(int key) {
    return searchKey(key);
}

std::vector<std::array<unsigned char, BPlusTree::TUPLE_SIZE>>
BPlusTree::readRangeData(int lowerKey, int upperKey) {
    std::vector<std::array<unsigned char, TUPLE_SIZE>> result;
    if (lowerKey > upperKey) return result;

    int leafPageId = findLeafPageForKey(lowerKey);

    while (leafPageId != -1) {
        char page[PAGE_SIZE];
        readPage(leafPageId, page);

        LeafNodePage leaf{};
        std::memcpy(&leaf, page, sizeof(leaf));

        for (int i = 0; i < leaf.numKeys; ++i) {
            int k = leaf.keys[i];
            if (k > upperKey) {
                return result;
            }
            if (k >= lowerKey && k <= upperKey) {
                std::array<unsigned char, TUPLE_SIZE> tup{};
                std::memcpy(tup.data(), leaf.values[i], TUPLE_SIZE);
                result.push_back(tup);
            }
        }

        leafPageId = leaf.nextLeafPageId;
    }
    return result;
}

// Insertion helpers 

bool BPlusTree::insertInLeaf(int leafPageId, int key, const unsigned char* data,
                             int& newChildPage, int& newChildKey) {
    char page[PAGE_SIZE];
    readPage(leafPageId, page);

    LeafNodePage leaf{};
    std::memcpy(&leaf, page, sizeof(leaf));

    // check duplicate key; overwrite
    for (int i = 0; i < leaf.numKeys; ++i) {
        if (leaf.keys[i] == key) {
            std::memcpy(leaf.values[i], data, TUPLE_SIZE);
            char buf[PAGE_SIZE];
            std::memset(buf, 0, sizeof(buf));
            std::memcpy(buf, &leaf, sizeof(leaf));
            writePage(leafPageId, buf);

            newChildPage = -1;
            return true;
        }
    }

    // If there is room, just insert
    if (leaf.numKeys < LeafNodePage::MAX_KEYS) {
        int i = leaf.numKeys - 1;
        while (i >= 0 && leaf.keys[i] > key) {
            leaf.keys[i + 1] = leaf.keys[i];
            std::memcpy(leaf.values[i + 1], leaf.values[i], TUPLE_SIZE);
            --i;
        }
        leaf.keys[i + 1] = key;
        std::memcpy(leaf.values[i + 1], data, TUPLE_SIZE);
        leaf.numKeys++;

        char buf[PAGE_SIZE];
        std::memset(buf, 0, sizeof(buf));
        std::memcpy(buf, &leaf, sizeof(leaf));
        writePage(leafPageId, buf);

        newChildPage = -1;
        return true;
    }

    // Leaf split 
    int total = leaf.numKeys + 1;

    int32_t tmpKeys[LeafNodePage::MAX_KEYS + 1];
    unsigned char tmpVals[LeafNodePage::MAX_KEYS + 1][TUPLE_SIZE];

    int iOld = 0, iNew = 0;
    bool inserted = false;
    while (iOld < leaf.numKeys) {
        if (!inserted && key < leaf.keys[iOld]) {
            tmpKeys[iNew] = key;
            std::memcpy(tmpVals[iNew], data, TUPLE_SIZE);
            inserted = true;
            ++iNew;
        } else {
            tmpKeys[iNew] = leaf.keys[iOld];
            std::memcpy(tmpVals[iNew], leaf.values[iOld], TUPLE_SIZE);
            ++iOld;
            ++iNew;
        }
    }
    if (!inserted) {
        tmpKeys[iNew] = key;
        std::memcpy(tmpVals[iNew], data, TUPLE_SIZE);
    }

    int mid = total / 2;

    // left (original)
    leaf.numKeys = mid;
    for (int k = 0; k < mid; ++k) {
        leaf.keys[k] = tmpKeys[k];
        std::memcpy(leaf.values[k], tmpVals[k], TUPLE_SIZE);
    }

    // right (new leaf)
    int newPageId = allocatePage();
    LeafNodePage newLeaf{};
    newLeaf.isLeaf         = 1;
    newLeaf.numKeys        = total - mid;
    newLeaf.parentPageId   = leaf.parentPageId;
    newLeaf.nextLeafPageId = leaf.nextLeafPageId;

    for (int k = 0; k < newLeaf.numKeys; ++k) {
        newLeaf.keys[k] = tmpKeys[mid + k];
        std::memcpy(newLeaf.values[k], tmpVals[mid + k], TUPLE_SIZE);
    }

    // fix sibling pointer
    leaf.nextLeafPageId = newPageId;

    char buf1[PAGE_SIZE];
    char buf2[PAGE_SIZE];
    std::memset(buf1, 0, sizeof(buf1));
    std::memset(buf2, 0, sizeof(buf2));

    std::memcpy(buf1, &leaf, sizeof(leaf));
    std::memcpy(buf2, &newLeaf, sizeof(newLeaf));

    writePage(leafPageId, buf1);
    writePage(newPageId, buf2);

    newChildPage = newPageId;
    newChildKey  = newLeaf.keys[0];  // first key of new leaf
    return true;
}

bool BPlusTree::insertRecursive(int pageId, int key, const unsigned char* data,
                                int& newChildPage, int& newChildKey) {
    char page[PAGE_SIZE];
    readPage(pageId, page);

    uint8_t isLeafFlag = *reinterpret_cast<uint8_t*>(page);

    if (isLeafFlag) {
        // leaf case
        return insertInLeaf(pageId, key, data, newChildPage, newChildKey);
    } else {
        // internal node
        InternalNodePage node{};
        std::memcpy(&node, page, sizeof(node));

        int i = 0;
        while (i < node.numKeys && key >= node.keys[i]) {
            ++i;
        }
        int childPageId = node.children[i];

        int childNewPage = -1;
        int childNewKey  = 0;

        bool ok = insertRecursive(childPageId, key, data,
                                  childNewPage, childNewKey);
        if (!ok) {
            return false;
        }

        // no split in child
        if (childNewPage == -1) {
            newChildPage = -1;
            return true;
        }

        // child split; need to insert (childNewKey, childNewPage)
        if (node.numKeys < InternalNodePage::MAX_KEYS) {
            // simple insert in this node
            for (int j = node.numKeys; j > i; --j) {
                node.keys[j]     = node.keys[j - 1];
                node.children[j + 1] = node.children[j];
            }
            node.keys[i]       = childNewKey;
            node.children[i+1] = childNewPage;
            node.numKeys++;

            char buf[PAGE_SIZE];
            std::memset(buf, 0, sizeof(buf));
            std::memcpy(buf, &node, sizeof(node));
            writePage(pageId, buf);

            newChildPage = -1;
            return true;
        }

        // internal node split 
        const int MAXK = InternalNodePage::MAX_KEYS;
        int32_t tmpKeys[MAXK + 1];
        int32_t tmpChildren[MAXK + 2];

        // merge existing keys/children and new one
        int idxKey = 0;
        int idxChild = 0;

        // children[0] always goes first
        tmpChildren[0] = node.children[0];
        idxChild = 1;

        for (int j = 0; j < node.numKeys; ++j) {
            if (j == i) {
                // insert new key & child
                tmpKeys[idxKey] = childNewKey;
                tmpChildren[idxChild] = childNewPage;
                ++idxKey;
                ++idxChild;
            }
            tmpKeys[idxKey] = node.keys[j];
            tmpChildren[idxChild] = node.children[j+1];
            ++idxKey;
            ++idxChild;
        }

        if (i == node.numKeys) {
            // new key goes at the very end
            tmpKeys[idxKey] = childNewKey;
            tmpChildren[idxChild] = childNewPage;
            ++idxKey;
            ++idxChild;
        }

        int totalKeys = node.numKeys + 1;
        int mid = totalKeys / 2;

        int32_t promoteKey = tmpKeys[mid];

        // left node (reuse pageId)
        InternalNodePage left{};
        left.isLeaf       = 0;
        left.parentPageId = node.parentPageId;
        left.numKeys      = mid;

        for (int j = 0; j < mid; ++j) {
            left.keys[j] = tmpKeys[j];
        }
        for (int j = 0; j <= mid; ++j) {
            left.children[j] = tmpChildren[j];
        }

        // right node (new page)
        InternalNodePage right{};
        right.isLeaf       = 0;
        right.parentPageId = node.parentPageId;
        right.numKeys      = totalKeys - mid - 1;

        for (int j = 0; j < right.numKeys; ++j) {
            right.keys[j] = tmpKeys[mid + 1 + j];
        }
        for (int j = 0; j <= right.numKeys; ++j) {
            right.children[j] = tmpChildren[mid + 1 + j];
        }

        // write left and right
        int newPageId = allocatePage();  // for right

        char bufLeft[PAGE_SIZE];
        char bufRight[PAGE_SIZE];
        std::memset(bufLeft, 0, sizeof(bufLeft));
        std::memset(bufRight, 0, sizeof(bufRight));

        std::memcpy(bufLeft, &left, sizeof(left));
        std::memcpy(bufRight, &right, sizeof(right));

        writePage(pageId, bufLeft);
        writePage(newPageId, bufRight);

        // set upward info
        newChildPage = newPageId;
        newChildKey  = promoteKey;
        return true;
    }
}

//  Public insert API 

bool BPlusTree::writeData(int key, const unsigned char* data) {
    FileHeader hdr = loadHeader();
    int rootPageId = hdr.rootPageId;

    int newChildPage = -1;
    int newChildKey  = 0;

    bool ok = insertRecursive(rootPageId, key, data, newChildPage, newChildKey);
    if (!ok) return false;

    if (newChildPage != -1) {
        // root split: create new root
        int oldRoot = rootPageId;
        int newRoot = allocatePage();

        InternalNodePage root{};
        root.isLeaf       = 0;
        root.parentPageId = -1;
        root.numKeys      = 1;
        root.keys[0]      = newChildKey;
        root.children[0]  = oldRoot;
        root.children[1]  = newChildPage;

        char buf[PAGE_SIZE];
        std::memset(buf, 0, sizeof(buf));
        std::memcpy(buf, &root, sizeof(root));
        writePage(newRoot, buf);

        hdr.rootPageId = newRoot;
        storeHeader(hdr);
    }

    return true;
}

// Deletion (leaf only, no rebalance) 

bool BPlusTree::removeFromLeaf(int leafPageId, int key) {
    char page[PAGE_SIZE];
    readPage(leafPageId, page);

    LeafNodePage leaf{};
    std::memcpy(&leaf, page, sizeof(leaf));

    int pos = -1;
    for (int i = 0; i < leaf.numKeys; ++i) {
        if (leaf.keys[i] == key) {
            pos = i;
            break;
        }
    }
    if (pos == -1) return false;

    for (int i = pos; i < leaf.numKeys - 1; ++i) {
        leaf.keys[i] = leaf.keys[i+1];
        std::memcpy(leaf.values[i], leaf.values[i+1], TUPLE_SIZE);
    }
    leaf.numKeys--;

    char buf[PAGE_SIZE];
    std::memset(buf, 0, sizeof(buf));
    std::memcpy(buf, &leaf, sizeof(leaf));
    writePage(leafPageId, buf);
    return true;
}

bool BPlusTree::deleteData(int key) {
    int leafPageId = findLeafPageForKey(key);
    return removeFromLeaf(leafPageId, key);
}
