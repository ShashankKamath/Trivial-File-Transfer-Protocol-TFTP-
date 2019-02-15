#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netdb.h>

#define NONE 0
#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5

#define max_buffer_length 520
int nextchar =-1;

/*********************************************************************************************************************
The getaddress is a user defined function which compares the family to IPv4 or IPv6 and returns the IP address.
*********************************************************************************************************************/
void *getaddress(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) 
	{
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
		return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*********************************************************************************************************************
The getport is a user defined function which compares the family to IPv4 or IPv6 and returns the port number.
*********************************************************************************************************************/
void *getport(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) 
	{
        return &(((struct sockaddr_in*)sa)->sin_port);
    }
		return &(((struct sockaddr_in6*)sa)->sin6_port);
}


void sigchld_handler(int signum)
{
    int saved_errno = errno;

    while (waitpid( -1, NULL, WNOHANG) > 0) ;
        
    errno = saved_errno;
}

int readable_timeo(int fd,int sec) 
{ 	
	fd_set rset;
	struct timeval tv;
	
	FD_ZERO(&rset);
	FD_SET(fd,&rset);
	
	tv.tv_sec= sec;
	tv.tv_usec=0;
	return (select(fd+1,&rset,NULL,NULL,&tv));/* >0 if descriptor is readable */
}

ssize_t read_file(FILE *fp,char *pointer,uint16_t block_number,char *send_buf)
{
	char c;
	int cnt;
	memset(send_buf,0,strlen(send_buf));
	pointer=send_buf;
	*pointer=0x00;
	pointer++;
	*pointer=0x03;//encoding as DATA
	pointer++;	
	if(block_number<=255) 
	{
		*pointer=0x00;
		pointer++;
		*pointer=block_number;
		pointer++;
	}
	else 
	{
		*pointer=((block_number)&(0xFF00))>>8;
		pointer++;
		*pointer=(block_number)&(0x00FF);
		pointer++;
	}
	
	for(cnt=0;cnt<512;cnt++) 
	{
		if(nextchar>=0) 
		{
			*pointer++ =nextchar;
			nextchar=-1;
			continue;
		}
		c=getc(fp);
		
		if(c==EOF) 
		{
			if(ferror(fp))
				printf("read err from getc on local file\n");
			return(cnt+4);
		}

		else if(c=='\n') 
		{
			c='\r';
			nextchar='\n';
		} 
		else if(c=='\r')
		{
			nextchar='\0';
		}
		else
			nextchar=-1;
		*pointer++=c;
	}
	cnt=516;
	return cnt;
}

ssize_t write_file(FILE *fp2,char *pointer,int recvbytes,char *recv_buf) 
{
pointer=recv_buf;
pointer=pointer+4;
char c,ch;
int cnt;
for(cnt=0;cnt<recvbytes-4;cnt++) 
{
	c=*pointer;
	pointer++;
	ch=*pointer;
	if((c=='\r' )&& (ch=='\n') ) 
	{
		putc(c,fp2);

	}
	else if((c=='\r') && (ch=='\0')) 
	{ 
		putc(c,fp2);

	}
	else 
	{
	putc(c,fp2);
	}
}
return cnt;
}

int main(int argc, char *argv[])
{	
	struct addrinfo addressinfo, *servicelist, *loopvariable;
	struct sockaddr_storage str_addr;
	struct sockaddr_in server_addr;
	socklen_t addr_size;
	struct sigaction sa;
	
	int socketfd, clientsocketfd;
	int opcode;
	int number_bytes=0;
	int num_bytes_sent=0;
	int num_bytes_rcv=0;
	int yes=1;
	int time_value=0;
	int flag=1;
	int flag1=1;
	
	unsigned char send_buf[max_buffer_length];
	unsigned char recv_buf[max_buffer_length];
	char buf[max_buffer_length]={0};
	char s[INET6_ADDRSTRLEN];

	pid_t process_id=0;
	
	//char c;

	memset(&addressinfo,0,sizeof (addressinfo)); // Making the addressinfo struct zero
	addressinfo.ai_family = AF_UNSPEC;// Not defining whether the connection is IPv4 or IPv6
	addressinfo.ai_socktype = SOCK_DGRAM;

	if(argc !=3)
	{
		printf("Server: Excess Arguments Passed \n");
		exit(1);
	}
	
	if ((flag = getaddrinfo(argv[1], argv[2], &addressinfo, &servicelist)) != 0) 
	{
		printf("GetAddrInfo Error");
		exit(1);
	}
	//printf("Server: Done with getaddrinfo \n");
	
	// Traversing the linked list for creating the socket
	for(loopvariable = servicelist; loopvariable != NULL; loopvariable = (loopvariable -> ai_next ))
	{
		if((socketfd = socket(loopvariable -> ai_family, loopvariable -> ai_socktype, loopvariable -> ai_protocol)) ==  -1 )
		{
			printf("Server: Listener Socket Created.\n");
			continue;
		}
		if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) ==  -1) 
		{
			perror("Server: SetSockOpt\n");
			exit(1);
		}
		//Binding the socket
		if (bind(socketfd, loopvariable->ai_addr, loopvariable->ai_addrlen) ==  -1)	
		{
			close(socketfd);
			perror("Server: Listerner Bind Error.\n");
			continue;
		}
		break;
	}
	// Freeing the linked  list
	freeaddrinfo(servicelist); 

	if (loopvariable == NULL) 
	{
		printf("Server: Listerner failed to bind.\n");
		exit(1);
	}
	printf("Server: Waiting for connections: \n");
	
	while(1)
	{
		addr_size = sizeof(str_addr);
		if(number_bytes = recvfrom(socketfd,buf,max_buffer_length-1,0,(struct sockaddr *)&str_addr,&addr_size)== -1)
		{
			perror("Server: recvfrom error");
			exit(1);
		}
		printf("Server: got packet from %s and port : %d \n",inet_ntop(str_addr.ss_family,getaddress((struct sockaddr *)&str_addr),s, sizeof s), ntohs(getport((struct sockaddr *)&str_addr)));
		buf[number_bytes]='\0';
		
		if ((process_id = fork()) ==  -1)
        {
           printf("Server: Fork error \n");
		   exit(1);
        }
        else if (process_id == 0)
        {
			opcode=buf[1];
			sa.sa_handler = sigchld_handler; 
			sigemptyset(&sa.sa_mask);
			sa.sa_flags = SA_RESTART;
			if (sigaction(SIGCHLD, &sa, NULL) == -1) 
			{
				perror("sigaction");
				exit(1);
			}
			
			server_addr.sin_family = AF_INET;
			server_addr.sin_addr.s_addr = htonl (INADDR_ANY); 
			server_addr.sin_port=htons(0); // Assigning a new port
			
			//Creating a new socket for the client 
			if((clientsocketfd = socket(AF_INET,SOCK_DGRAM,0)) == -1)
			{
				perror("Server: New Client Socket error\n");
				exit(1);
			}
			if (setsockopt(clientsocketfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1)
			{
				perror("Server: Client SetSockOpt\n");
				exit(1);
			}
			if (bind(clientsocketfd, (struct sockaddr *)&server_addr, sizeof server_addr) == -1)
			{
				close(clientsocketfd);
				perror("Server_Client Bind error");
				exit(1);
			}

			char filename[30];
			char mode[10];
			
			//Getting the filename from the input
			strcpy(filename,&buf[2]);//check this 2 in buffer
			filename[strlen(filename)]='\0';
			//printf("Filename %s \n",filename);
			strcpy(mode,&buf[3+strlen(filename)]);
			mode[strlen(mode)]='\0';
			//printf("Mode %s \n",mode);
			
			//Checking the packet type for RRQ
			if(opcode == RRQ)
			{
				if(!strcmp(mode,"netascii"))
				{
					FILE *fp;
					fp=fopen(filename,"r");
					unsigned char err_msg[520];
					char print_msg[30]="FIile Does not exist";
					int len=strlen(print_msg);
					char *pointer4;
					if(fp==NULL)
					{
						printf("File does not exist\n");
						memset(err_msg,0,strlen(err_msg));
						pointer4=err_msg;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x05;
						pointer4++;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x06;
						pointer4++;
						strcpy(pointer4,print_msg);
						pointer4=pointer4+len;
						*pointer4=0;
						if((num_bytes_sent=sendto(clientsocketfd,err_msg,24,0,(struct sockaddr*)&str_addr,addr_size))==-1)
						{
							perror("Error Message send error");
						}
						printf("Client Disconnected \n");
						close(clientsocketfd);
						exit(1);
						
					}
					
					
					uint16_t block_number=1;
					uint16_t block_number_rcv=0;
					char *pointer1;
					int count1;
					while((count1=read_file(fp,pointer1,block_number,send_buf))<=516)
					{
						if(count1==0) {break;}
						int time_count =0;
						flag1=1;
						do
						{
							if((num_bytes_sent=sendto(clientsocketfd,send_buf,count1,0,(struct sockaddr *)&str_addr, addr_size))== -1)
							{
								perror("Server: Sendto error\n");
								break;
							}
							//printf("Number of bytes sent: %d \n",num_bytes_sent);
							while((time_value<=0 || time_value==1) && (time_count<=10))
							{
								if((time_value=readable_timeo(clientsocketfd,1))==0)
								{
									printf("Server: TImeout\n");
									time_count++;
									flag1=1;
									break;
									
								}
								else if(time_value==-1)
								{
									printf("Server: Timeout Error\n");
								}
								else
								{
									printf("Server: Socket Ready\n");
									break;
								}
							}
							if(time_count>10)
							{
								printf("Server: Timeout Occured\n");
								fclose(fp);
								close(clientsocketfd);
								flag1=0;
								exit(1);
								
							}
							memset(recv_buf,0,strlen(recv_buf));
							if((num_bytes_rcv=recvfrom(clientsocketfd,recv_buf,max_buffer_length-1,0,(struct sockaddr *)&str_addr,&addr_size)) == -1)
							{
								perror("Server: Client receive error\n");
								break;
							}
							else
							{
								if(recv_buf[1]==ACK)
								{
									block_number_rcv=(recv_buf[2]<<8 | recv_buf[3]);
									if(block_number_rcv==block_number)
									{
										printf("Packet %d acknowledged\n",block_number);
										block_number++;
										time_value=0;
										flag1=0;
									}
									else
									{
										flag1=1;
										//goto back1;
										time_value=0;
									}
								}
							
							}	
						}while(flag1);//do while end
						
						if(count1>=0 && count1<516)
						{	
							printf("Final ACK has been received\n");
							printf("Server: Transfer Done\n");
							break;
							flag1=0;
						}
					}//end of while(count)	
				}//netascii end
				
				if(!strcmp(mode,"octet"))
				{
						ssize_t send_bytes;
						char *pointer1;
						pointer1=send_buf; 
						uint16_t block_number=1;
						int fp1;
						fp1=open(filename,O_RDONLY);
						unsigned char err_msg[520];
					char print_msg[30]="FIile Does not exist";
					int len=strlen(print_msg);
					char *pointer4;
					if(fp1==-1)
					{
						printf("File does not exist\n");
						memset(err_msg,0,strlen(err_msg));
						pointer4=err_msg;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x05;
						pointer4++;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x06;
						pointer4++;
						strcpy(pointer4,print_msg);
						pointer4=pointer4+len;
						*pointer4=0;
						if((num_bytes_sent=sendto(clientsocketfd,err_msg,24,0,(struct sockaddr*)&str_addr,addr_size))==-1)
							{
								perror("Error Message send error");
							}
						printf("Client Disconnected \n");
						close(clientsocketfd);
						exit(1);
						
					}
					flag1=1;
					while(1)
					{
						memset(send_buf,0,strlen(send_buf));
						pointer1=send_buf;
						*pointer1=0x00;
						pointer1++;
						*pointer1=0x03;
						pointer1++;
						
						if(block_number<=255) 
						{
					
							*pointer1=0x00;
							pointer1++;
							*pointer1=block_number;
							pointer1++;
						}
						else 
						{
							*pointer1=((block_number)&(0xFF00))>>8;
							pointer1++;
							*pointer1=(block_number)&(0x00FF);
							pointer1++;
						}
						send_bytes=read(fp1,pointer1,512);
						
						int time_count=0;
						do
						{
						
						if(num_bytes_sent=sendto(clientsocketfd,send_buf,send_bytes+4,0,(struct sockaddr *)&str_addr, addr_size) == -1)
						{
							perror("Server: Error in sending in octet mode\n");
						}
						

						
						while((time_value<=0 || time_value==1) && (time_count<=10))
						{
							if((time_value=readable_timeo(clientsocketfd,1))==0)
							{
								printf("Server: TImeout\n");
								time_count++;
								flag1=1;
								//break;
								
							}
							else if(time_value==-1)
							{
								printf("Server: Timeout Error\n");
							}
							else
							{
								//printf("Server: Socket Ready\n");
								break;
							}
						}//timeout while end
						
						if(time_count>10)
						{
							printf("Server: Timeout Occured\n");
							//fp1=0;
							close(clientsocketfd);
							flag1=0;
							exit(1);
							
						}
						
						memset(recv_buf,0,strlen(recv_buf));
						if(num_bytes_rcv=recvfrom(clientsocketfd,recv_buf,max_buffer_length-1,0,(struct sockaddr *)&str_addr,&addr_size)== -1)
						{
							perror("Receive error");
						}
						else
						{
							if(recv_buf[1]==ACK)
							{
								uint16_t block_number_rcv=(recv_buf[2]<<8)|(recv_buf[3]);
								if(block_number_rcv == block_number)
								{
									printf("Packet %d acknowledged\n",block_number);
									block_number++;
									flag1=0;
									
								}	
								else
								{
									flag1=1;
									time_value=0;
								}
							}//ACK end
						}
					}while(flag1);//end do here
					
					if(send_bytes>=0 && send_bytes<512)
						{	
							printf("Final ACK received\n");
							printf("File transfer done\n");
							break;
						}
				}//end of while
				}//octet end	
			close(clientsocketfd);	
			}//RRQ end
			
			if(opcode == WRQ)
			{
				if(!strcmp(mode,"netascii"))
				{
					char *pointer3, *pointer4;
					unsigned char err_msg[520];
					char print_msg[30]="FIile Exists";
					uint16_t block_number =1;
					int len=strlen(print_msg);
					uint16_t block_number_rcv;
					int written_count;
					pointer3=send_buf;
					*pointer3=0x00;
					pointer3++;
					*pointer3=ACK;
					pointer3++;
					*pointer3=0x00;
					pointer3++;
					*pointer3=0x00;
					pointer3++;
					
					if((num_bytes_sent=sendto(clientsocketfd,send_buf,4,0,(struct sockaddr*)&str_addr,addr_size))==-1)
					{
						perror("Server-Client: Send to error");
					}
					FILE *fp2;
					fp2=fopen(filename,"wx");
					if(fp2==NULL)
					{
						printf("File already exists");
						memset(err_msg,0,strlen(err_msg));
						pointer4=err_msg;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x05;
						pointer4++;
						*pointer4=0x00;
						pointer4++;
						*pointer4=0x06;
						pointer4++;
						strcpy(pointer4,print_msg);
						pointer4=pointer4+len;
						*pointer4=0;
						if((num_bytes_sent=sendto(clientsocketfd,err_msg,24,0,(struct sockaddr*)&str_addr,addr_size))==-1)
						{
							perror("Error Message send error");
						}
						close(clientsocketfd);
						exit(1);
					}
					flag1=1;
					do
					{
						memset(recv_buf,0,strlen(recv_buf));
						if((num_bytes_rcv=recvfrom(clientsocketfd,recv_buf,max_buffer_length-4,0,(struct sockaddr *)&str_addr,&addr_size))== -1)
						{
							perror("Client receive error\n");
						}
						recv_buf[num_bytes_rcv]='\0';
						////print here if needed
						
						if(recv_buf[1]==DATA)
						{
							block_number_rcv=(recv_buf[2]<<8)| (recv_buf[3]);
							pointer3=recv_buf;
							if(block_number_rcv==block_number)
							{
								written_count=write_file(fp2,pointer3,num_bytes_rcv,recv_buf);
								memset(send_buf,0,strlen(send_buf));
								pointer3=send_buf;
								*pointer3=0x00;
								pointer3++;
								*pointer3=ACK;
								pointer3++;
											
								if(block_number_rcv<=255) 
								{
								
									*pointer3=0x00;
									pointer3++;
									*pointer3=block_number_rcv;
									pointer3++;
								}
								else 
								{
									*pointer3=((block_number_rcv)&(0xFF00))>>8;
									pointer3++;
									*pointer3=(block_number_rcv)&(0x00FF);
									pointer3++;
								}
								
								if((num_bytes_sent=sendto(clientsocketfd,send_buf,4,0,(struct sockaddr *)&str_addr, addr_size)) == -1) 
								{
									perror("server_child_sendto error");
								}
								block_number++;
								if(num_bytes_rcv<516) 
								{
								flag1=0;
								close(clientsocketfd);
								exit(1);
								}

								else if(num_bytes_rcv==516) 
								{
								flag1=1;
								} 
							}
						}
					}while(flag1);	
				}//netascii end
				
				if(!strcmp(mode,"octet"))
				{
					char *pointer3, *pointer4;
					unsigned char err_msg[520];
					char print_msg[30]="FIile Exists";
					uint16_t block_number =1;
					int len=strlen(print_msg);
					uint16_t block_number_rcv;
					int written_count;
					pointer3=send_buf;
					*pointer3=0x00;
					pointer3++;
					*pointer3=ACK;
					pointer3++;
					*pointer3=0x00;
					pointer3++;
					*pointer3=0x00;
					pointer3++;
					
					if((num_bytes_sent=sendto(clientsocketfd,send_buf,4,0,(struct sockaddr*)&str_addr,addr_size))==-1)
					{
						perror("Server-Client: Send to error");
					}
					int fp3;
					fp3=open(filename,O_WRONLY|O_CREAT|O_EXCL,0644);
					if(fp3==-1)
					{ 
						if(errno==EEXIST)
						{
							printf("File already exists");
							memset(err_msg,0,strlen(err_msg));
							pointer4=err_msg;
							*pointer4=0x00;
							pointer4++;
							*pointer4=0x05;
							pointer4++;
							*pointer4=0x00;
							pointer4++;
							*pointer4=0x06;
							pointer4++;
							strcpy(pointer4,print_msg);
							pointer4=pointer4+len;
							*pointer4=0;
							if((num_bytes_sent=sendto(clientsocketfd,err_msg,24,0,(struct sockaddr*)&str_addr,addr_size))==-1)
							{
								perror("Error Message send error");
							}
							close(clientsocketfd);
							exit(1);
						}
					}
					flag1=1;
					do
					{
						memset(recv_buf,0,strlen(recv_buf));
						if((num_bytes_rcv=recvfrom(clientsocketfd,recv_buf,max_buffer_length-4,0,(struct sockaddr *)&str_addr,&addr_size))== -1)
						{
							perror("Client receive error\n");
						}
						recv_buf[num_bytes_rcv]='\0';
						////print here if needed
						
						if(recv_buf[1]==DATA)
						{
							block_number_rcv=(recv_buf[2]<<8)| (recv_buf[3]);
							
							if(block_number_rcv==block_number)
							{
								write(fp3,&recv_buf[4],num_bytes_rcv-4);
								memset(send_buf,0,strlen(send_buf));
								pointer3=send_buf;
								*pointer3=0x00;
								pointer3++;
								*pointer3=ACK;
								pointer3++;
											
								if(block_number_rcv<=255) 
								{
									*pointer3=0x00;
									pointer3++;
									*pointer3=block_number_rcv;
									pointer3++;
								}
								else 
								{
									*pointer3=((block_number_rcv)&(0xFF00))>>8;
									pointer3++;
									*pointer3=(block_number_rcv)&(0x00FF);
									pointer3++;
								}
								
								if((num_bytes_sent=sendto(clientsocketfd,send_buf,4,0,(struct sockaddr *)&str_addr, addr_size)) == -1) 
								{
									perror("server_child_sendto error");
								}
								block_number++;
								if(num_bytes_rcv<516) 
								{
								printf("File transfer Complete\n");
								flag1=0;
								close(clientsocketfd);
								exit(1);
								}

								else if(num_bytes_rcv==516) 
								{
								flag1=1;
								} 
							}
						}
					}while(flag1);	
					}//octet end
			}//end WRQ	
        }//Process_id creation
	}//Infinite While loop
	close(socketfd);
	return 0;
}//Main
