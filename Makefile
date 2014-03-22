CPP	= g++

CFLAGS = -Wno-invalid-offsetof -Ofast -flto -fwhole-program

SRC := matcher.cpp parser.cpp regex.cpp tools.cpp

OBJ	= $(SRC:%.cpp=%.o)

BIN	= regex

all: $(BIN)

.cpp.o:
	$(CPP) $(CFLAGS) -c $< -o $@

$(BIN):\
	$(OBJ) 
	$(CPP) $(CFLAGS) -o $@ $(OBJ)

$(OBJ): matcher.h parser.h regex.h tools.h

clean:; rm -f $(OBJ) $(BIN) core
