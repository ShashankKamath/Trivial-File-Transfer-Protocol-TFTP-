########################################################################


#Team 6 Tftp server makefile


#######################################################################


build:tftp_server.c

	gcc tftp_server.c -o tftp_server
    
clean:

	-rm -rf tftp_server
