CC=g++
FLAGS=-g -std=gnu++11 -Wall -Wextra
LIBS=-lpthread
RM = rm -f

all: process_per_protocol 

#process_per_message: process_per_message threadpool.o message.o
#	$(CC) $(FLAGS) -o $@ $^ $(LIBS)

process_per_protocol: process_per_protocol.cpp threadpool.o message.o
	$(CC) $(FLAGS) -o $@ $^ $(LIBS)

threadpool.o: threadpool.cpp
	$(CC) $(FLAGS) -o $@ -c $^

message.o: message.cpp
	$(CC) $(FLAGS) -o $@ -c $^

clean:
	$(RM) threadpool.o message.o process_per_protocol 