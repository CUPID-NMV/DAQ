TARGET = CAEN_PLULibTEST
CXXFLAGS = -std=c++11 -O2 -g -Wall
LDFLAGS =
LDLIBS = -lCAEN_PLU

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = CAEN_PLULibTEST.o

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
