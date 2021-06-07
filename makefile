all: chat.o instructorList.o
	gcc chat.c instructorList.o -o chat -pthread

chat.o: chat.c
	gcc -pthread -c chat.c

clean:
	rm chat.o chat
