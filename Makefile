SRCS = \
	main.cxx

OBJS = $(subst .c,.o,$(subst .cc,.o,$(subst .cxx,.o,$(SRCS))))

ifeq ($(OS), Windows_NT)
CXXFLAGS = -Wall -O2 -I. -Ic:/msys64/mingw64/include/c++/10.1.0 -IC:/msys64/mingw64/include/c++/10.1.0/x86_64-w64-mingw32
LIBS = -O2 -Lc:/msys64/mingw64/lib -Lc:/msys64/mingw64/x86_64-w64-mingw32/lib -LC:/msys64/mingw64/lib/gcc/x86_64-w64-mingw32/10.1.0 -lws2_32
TARGET = main.exe
else
CXXFLAGS = -Wall -O2 -I.
LIBS =
TARGET = main
endif

.SUFFIXES: .cxx .hpp .c .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) -std=c++17 -o $@ $(OBJS) $(LIBS)

.cxx.o :
	$(CXX) -std=c++17 -c $(CXXFLAGS) -I. $< -o $@

.cpp.o :
	$(CXX) -std=c++17 -c $(CXXFLAGS) -I. $< -o $@

.c.o :
	$(CC) -c $(CXXFLAGS) -I. $< -o $@

clean :
	rm -f *.o $(TARGET)

