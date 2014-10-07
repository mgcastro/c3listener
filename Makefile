c3listener: main.c
	gcc -o c3listener -lbluetooth main.c

clean:
	rm -f *.o c3listener
