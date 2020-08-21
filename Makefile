CC = gcc
CFLAGS = -I.

BINARY = Chip-84

ODIR = obj
LDLIBS = -pthread

DEPS = chip8.h
_OBJ = main.o chip8.o
OBJ = $(patsubst %, $(ODIR)/%, $(_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	@mkdir -p $(@D)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BINARY): $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LDLIBS)
	
.PHONY: clean

clean:
	rm -f $(ODIR)/*.o $(BINARY) -r $(ODIR)
