#include "bptree.h"

#include <iostream>
#include <cstring>

int main() {
    try {
        BPlusTree tree("index.dat");

        unsigned char buf[BPlusTree::TUPLE_SIZE];

        std::cout << "Inserting 30 keys...\n";
        for (int k = 1; k <= 30; ++k) {
            std::memset(buf, 0, sizeof(buf));
            std::snprintf(reinterpret_cast<char*>(buf),
                          sizeof(buf),
                          "value_%d", k);
            tree.writeData(k, buf);
        }

        // Single key read
        int keyToRead = 10;
        const unsigned char* res = tree.readData(keyToRead);
        if (res) {
            std::cout << "readData(" << keyToRead << ") = "
                      << reinterpret_cast<const char*>(res) << "\n";
        } else {
            std::cout << "readData(" << keyToRead << ") = NOT FOUND\n";
        }

        // Range read
        int L = 5, R = 15;
        std::cout << "\nRange [" << L << ", " << R << "] results:\n";
        auto range = tree.readRangeData(L, R);
        int keyGuess = L;
        for (const auto& tup : range) {
            std::cout << "  ~ "
                      << reinterpret_cast<const char*>(tup.data())
                      << "\n";
            ++keyGuess;
        }

        // Delete some keys
        std::cout << "\nDeleting key 10...\n";
        bool delOk = tree.deleteData(10);
        std::cout << "deleteData(10) -> " << (delOk ? "ok" : "fail") << "\n";

        res = tree.readData(10);
        std::cout << "After delete, readData(10) -> "
                  << (res ? reinterpret_cast<const char*>(res) : "NOT FOUND")
                  << "\n";

        std::cout << "\nDeleting key 1...\n";
        delOk = tree.deleteData(1);
        std::cout << "deleteData(1) -> " << (delOk ? "ok" : "fail") << "\n";

        res = tree.readData(1);
        std::cout << "After delete, readData(1) -> "
                  << (res ? reinterpret_cast<const char*>(res) : "NOT FOUND")
                  << "\n";

        std::cout << "\nDone.\n";
    } catch (const std::exception& ex) {
        std::cerr << "Exception: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
