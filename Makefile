CC = gcc

SRCDIR = src
OBJDIR = obj
BINDIR = bin
INCLUDEDIR = include
DUMPSDIR = dumps
RECVDIR = recvfiles

CFLAGS = -std=gnu99 -g -O3 -I$(INCLUDEDIR)
LFLAGS = -lm -pthread

INCLUDE = $(INCLUDEDIR)/*.h

OBJ = $(OBJDIR)/handle.o \
      $(OBJDIR)/ring_buffer.o

default: all

all: $(BINDIR)/tinytcp

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCLUDE)
	$(CC) -c -o $@ $< $(CFLAGS)

$(BINDIR)/%: $(OBJDIR)/%.o $(OBJ)
	$(CC) -o $@ $^ $(LFLAGS)

.PRECIOUS: $(OBJDIR)/%.o

.PHONY: default all clean

clean:
	rm -f $(OBJDIR)/*.o; rm -f $(BINDIR)/*; rm -f $(DUMPSDIR)/*; rm -f $(RECVDIR)/*;
