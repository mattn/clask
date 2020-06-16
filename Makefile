SRCS = \
	main.cxx

OBJS = $(subst .c,.o,$(subst .cc,.o,$(subst .cxx,.o,$(SRCS))))

ifeq ($(OS), Windows_NT)
CXXFLAGS = -std=c++17 -I../.. -I. -Wall -O2
LIBS = -lws2_32
TARGET = main.exe
else
CXXFLAGS = -std=c++17 -I../.. -I. -Wall -O2
LIBS = -lstdc++fs -lpthread
TARGET = main
endif

.PHONY: test

.SUFFIXES: .cxx .hpp .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

.cxx.o :
	$(CXX) -c $(CXXFLAGS) $< -o $@

clean :
	-rm -f *.o $(TARGET) clask_teset

test : picotest
	$(CXX) $(CXXFLAGS) -Ipicotest -o clask_test test.cxx picotest/picotest.c $(LIBS)
	./clask_test

picotest :
	git clone https://github.com/h2o/picotest
