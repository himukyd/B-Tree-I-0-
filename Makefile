CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

TARGET   = driver
OBJS     = bptree.o driver.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

bptree.o: bptree.cpp bptree.h
	$(CXX) $(CXXFLAGS) -c bptree.cpp

driver.o: driver.cpp bptree.h
	$(CXX) $(CXXFLAGS) -c driver.cpp

clean:
	rm -f $(OBJS) $(TARGET) index.dat
