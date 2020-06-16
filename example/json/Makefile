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

.SUFFIXES: .cxx .hpp .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LIBS)

.cxx.o :
	$(CXX) -c $(CXXFLAGS) -I. $< -o $@

clean :
	rm -f *.o $(TARGET)
