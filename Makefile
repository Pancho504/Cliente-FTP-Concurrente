# Makefile para cliente FTP 

CLTOBJ= TCPftp.o connectsock.o connectTCP.o passivesock.o passiveTCP.o errexit.o

all: TCPftp 

TCPftp:	${CLTOBJ}
	cc -o TCPftp ${CLTOBJ}

clean:
	rm $(CLTOBJ) 
