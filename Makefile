c3listener: main.c
	$(CC) -std=gnu99 -Wall -o c3listener -lbluetooth -ljson-c -lcurl -lconfig -lavahi-client -lavahi-common main.c
