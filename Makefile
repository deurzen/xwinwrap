all:
	gcc xwinwrap.c -o xwinwrap `pkg-config --libs x11 xext xrender`

install:
	install xwinwrap /usr/bin/

clean:
	-rm -f xwinwrap
