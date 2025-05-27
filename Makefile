#CFLAGS 	= -Wall -Wextra -O2 `pkg-config --cflags sqlite3 openssl`
#LDFLAGS = `pkg-config --libs sqlite3 openssl`

CC 	= gcc
CFLAGS 	= -Wall -Wextra -O2 -I/opt/homebrew/Cellar/openssl@3/3.5.0/include
LDFLAGS = -lsqlite3 -L/opt/homebrew/Cellar/openssl@3/3.5.0/lib -lssl -lcrypto

OBJS 	= main.o registry.o
BIN	= Itool

all: $(BIN)

Itool: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clobber: clean
	-rm -f $(BIN)

clean:
	-rm -f $(OBJS)

zip:
	zip
