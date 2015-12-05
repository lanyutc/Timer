CC = g++
FLAGS  = -Wall -g -MMD -m64
LIBS   = -lpthread 


SRCS    = $(wildcard *.cpp)
BINS    = test

all: $(BINS)


$(BINS): $(SRCS:.c=.o)
	$(CC)  $(FLAGS) -o $@ $^ $(LIBS)

%.o: %.cpp
	$(CC) $(FLAGS) $(DDEFINE) $(INCLUDE) -c -o $@ $<
	@-mv -f *.o *.d .dep.$@

clean:
	@-rm -f *.o *.oxx *.po *.so *.d .dep.* $(BINS)

-include /dev/null $(wildcard .dep.*)
