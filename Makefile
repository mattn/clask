SRCS = \
	main.cxx

OBJS = $(subst .c,.o,$(subst .cc,.o,$(subst .cxx,.o,$(SRCS))))

ifeq ($(OS), Windows_NT)
CXXFLAGS = -std=c++17 -Wall -O2 -I. -Ic:/msys64/mingw64/include/c++/10.1.0 -IC:/msys64/mingw64/include/c++/10.1.0/x86_64-w64-mingw32
LIBS = -std=c++17 -O2 -Lc:/msys64/mingw64/lib -Lc:/msys64/mingw64/x86_64-w64-mingw32/lib -LC:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/10.1.0 -lws2_32
TARGET = main.exe
else
CXXFLAGS = -std=c++17 -Wall -O2 -I.
LIBS =
TARGET = main
endif

.PHONY: test

.SUFFIXES: .cxx .hpp .c .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

.cxx.o :
	$(CXX) -c $(CXXFLAGS) $< -o $@

.c.o :
	$(CC) -c $(CXXFLAGS) $< -o $@

clean :
	rm -f *.o $(TARGET)

test :
	g++ -std=c++17 -I. -o clask_test test.cxx -lgtest -lgtest_main $(LIBS)
	./clask_test
