SRCS = \
	main.cxx

OBJS = $(subst .c,.o,$(subst .cc,.o,$(subst .cxx,.o,$(SRCS))))

ifeq ($(OS), Windows_NT)
CXXFLAGS = -I../.. -I. -O2
LIBS = -O2 -lws2_32
TARGET = main.exe
else
CXXFLAGS = -I../.. -I. -O2
LIBS = -O2
TARGET = main
endif

.SUFFIXES: .cxx .hpp .c .o

all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) -std=c++17 -o $@ $(OBJS) $(LIBS)

.cxx.o :
	$(CXX) -std=c++17 -c $(CXXFLAGS) -I. $< -o $@

.c.o :
	$(CC) -c $(CXXFLAGS) -I. $< -o $@

clean :
	rm -f *.o $(TARGET)
