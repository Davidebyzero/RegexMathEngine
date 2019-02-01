CPP	= g++

CFLAGS = -Wno-invalid-offsetof -Ofast -flto -fwhole-program

SRC := matcher.cpp math-optimization.cpp parser.cpp regex.cpp tools.cpp

ifdef USE_GMP
CFLAGS := $(CFLAGS) -DUSE_GMP
LFLAGS = -lgmp
endif

OBJ	= $(SRC:%.cpp=%.o)

BIN	= regex

all: $(BIN)

.cpp.o:
	$(CPP) $(CFLAGS) -c $< -o $@

$(BIN):\
	$(OBJ) 
	$(CPP) $(CFLAGS) -o $@ $(OBJ) $(LFLAGS)

$(OBJ): matcher.h matcher-optimization.h math-optimization.h parser.h regex.h tools.h

clean:; rm -f $(OBJ) $(BIN) core
