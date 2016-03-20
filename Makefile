CPPFLAGS  =-std=c++11 -Wall -Wextra

all: libuthreads.a

libuthreads.o: uthreads.cpp uthreads.h
	gcc $(CPPFLAGS) -c uthreads.cpp
	
libuthreads.a: libuthreads.o
	ar rcs libuthreads.a uthreads.o
	
clean:
	rm -f *.o *.a libuthreads
