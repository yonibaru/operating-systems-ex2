CC = gcc
CFLAGS = -Wall
all: mync ttt 

# part 1
ttt: ttt.c
	$(CC) $(CFLAGS) ttt.c -o ttt
# # part 2
# mync: mync.c
# 	$(CC) $(CFLAGS) mync.c -o mync
# part 3
mync2: mync2.c
	$(CC) $(CFLAGS) mync2.c -o mync2
# clean
clean:
	rm -f *.o *.a mync ttt mync2