#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>


#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //for exit(0);
#include <sys/socket.h>
#include <errno.h> //For errno - the error number
#include <netdb.h> //hostent
#include <arpa/inet.h>
#include <ctype.h>


#include "const.h"
#include "type.h"
#include "function.h"
#include "net.h"

/**
 * Clone a memory space
 * @param Pointer point to the memory space that need clone
 * @param Size of memory space that need clone
 * @param Padding space. Usually used for string termination character.
 * @return Pointer point to new memory space cloned from the input.
 */
char* clone(char* buffer, unsigned int size, unsigned int padding)
{
    /**
    * Example use:
    * char str1[] = "abc";
    * char* str2 = clone(str1, 3, 1);
    * str2[3] = '\0';
    * Sometime, str1 is in raw format without string termination character '\0'
    */
    return memcpy((char*)malloc(sizeof(char)*(size+padding)), buffer, size);
}

/**
 * Connect to FTP server and return hello message
 * @param FTP infomation
 * @return 0 if success, 1 if fail.
 */
int ftp_connect(struct FTPClient* ftp)
{
    if (portConnect(&(ftp->cmd.sockfd), &(ftp->cmd.port), ftp->host, FTP_CMD_PORT))
	{
		printf("[ERROR] Could not connect to FTP service at %s\n", ftp->host);
		return 1;
	}

	char *hello = ftp_comm(ftp->cmd.sockfd, NULL, 0);
    printf("%s", hello);
    free(hello);

	return 0;
}

/**
 * FTP Authentication
 * @param FTP infomation
 * @return 0 if success, 1 if fail.
 */
int ftp_auth(struct FTPClient* ftp)
{
    ftp->user = (char*)malloc(BUFFSIZE_VAR);
    ftp->pass = (char*)malloc(BUFFSIZE_VAR);
    char* response;
    char cmd[BUFFSIZE_VAR];

    printf("Username: \n");
    //fgets(ftp->user, BUFFSIZE_VAR, stdin);
	ftp->user = "123bao";
	sprintf(cmd, "USER %s\r\n", ftp->user);
	response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
    printf("%s", response);		// Don't care about return code. Always 331 :3

    printf("Password: \n");
    ftp->pass = "Admin2355";
    //strcpy(ftp->pass, getpass("Password: "));
	sprintf(cmd, "PASS %s\r\n", ftp->pass);
	response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
    printf("%s", response);

    // return code
    int rc;
    sscanf(response, "%d", &rc);
    if (rc != 230)			// FTP command 230 indicate
        return 1;
	else return 0;
}

/**
 * Switch FTP mode between active and passive
 * @param FTP infomation
 * @param Mode to be switched
 * @return 0 if success, 1 if fail.
 */
int ftp_mode(struct FTPClient* ftp, int mode)
{
    // Default FTP mode is active
    if (ftp->mode == 0)
        ftp->mode = FTP_ACTIVE;

	// Exception of this function

	// Don't re-establish connection
    if (sockStatus(ftp->data.sockfd) != SOCK_FREE && ftp->mode == mode)
		return 0;

	// Invalid mode
    if (mode != FTP_ACTIVE && mode != FTP_PASSIVE)
		return 1;

	// Working here
	char cmd[BUFFSIZE_VAR];
    uint8_t *ipv4;
    uint8_t *port;
    char* response;
	// return code
	int rc;

    if (mode == FTP_ACTIVE)
	{
		ftp->data.port = 0;		// Choose random port to listen
        portListen(&(ftp->data.sockfd), &(ftp->data.port));

        printf("[INFO] Client use data port %d\n", ftp->data.port);

		// Ref: https://linux.die.net/man/3/inet_aton
		struct in_addr addr;
		char* ip = getLocalIP();
        if (ip == NULL)
		{
            printf("[ERROR] ftp_mode(): No IPv4 associated\n");
            return 1;
		}
		inet_aton(ip, &addr);

		uint8_t *ipv4 = (uint8_t *)&addr.s_addr;

        sprintf(cmd, "PORT %d,%d,%d,%d,%d,%d\r\n", ipv4[0], ipv4[1], ipv4[2], ipv4[3], ftp->data.port/256, ftp->data.port%256);

        response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
        printf("%s", response);

		sscanf(response, "%d", &rc);
		if (rc != 150)			// FTP command 230 indicate successull login
			return 1;
		else return 0;
	}

    if (mode == FTP_PASSIVE)
	{
		// core here
        sprintf(cmd, "PASV\r\n");

        response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
        printf("%s", response);
		int port[2],ip[4];
		sscanf(response,"227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r",&ip[0],&ip[1],&ip[2],&ip[3],&port[0],&port[1]);
		printf("Port received though PORT command return: %d\n",port[0]*256+port[1]);
		ftp->data.port=0;
		if (portConnect(&(ftp->data.sockfd), &(ftp->data.port), ftp->host, port[0]*256+port[1]))
		{
			printf("[ERROR] Could not connect to FTP service at %s\n", ftp->host);
			return 1;
		}

		printf("Connected to Server with PASSIVE MODE!\n");
	}
}

/**
 * FTP send & receive command/data
 * @param FTP infomation
 * @param command to be sent, can be NULL
 * @param length of command
 * @return data returned by server
 */
char* ftp_comm(int sockfd, char* data, int len)
{
	// Used for SOCK_LISTEN
	struct sockaddr_in clientaddr;
	socklen_t addrlen = sizeof(clientaddr);

    switch (sockStatus(sockfd))
    {
		case SOCK_FREE:
			return NULL;

		case SOCK_LISTEN:
			// If socket is in listen mode. Accept any incomming connection.
			// sockfd now become client. Dont care about sockfd of the server
			sockfd = accept(sockfd, (struct sockaddr *) &clientaddr, &addrlen);
			break;

		case SOCK_NON_LISTEN:
			if (data != NULL)
			{
				len = write(sockfd, data, len);
				if (len < 0)
				{
					printf("[ERROR] write(): Could not write data to socket\n");
					return NULL;
				}
			}
			break;
    }

	char* response = (char*)malloc(BUFFSIZE_DATA);
    memset(response, 0, BUFFSIZE_DATA);
    read(sockfd, response, BUFFSIZE_DATA);

	return response;
}

int ftp_loop(struct FTPClient *ftp)
{
	char input[BUFFSIZE_VAR];
	char st[BUFFSIZE_VAR];
	char st1[BUFFSIZE_VAR];
    char cmd[BUFFSIZE_VAR];
	char data[BUFFSIZE_DATA];
	char *response;
	int command_code;
	int size;

    while (1)
	{
        printf("ftp> ");
        fgets(input, BUFFSIZE_VAR, stdin);
		if (strstr(input," ")!=NULL)
		{
			sscanf(input,"%s %s",&st1,&st);
		}
        if (strstr(input, "ls"))
		{
			char *data;
			sprintf(cmd,"LIST\r\n");
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			printf("%s", response);
			while(1)
			{
				int flag=0;
				data = ftp_comm(ftp->data.sockfd, NULL, 0);
				printf("%s",data);
				for (int i=0;i<strlen(data);i++)
					if (data[i]!=0)
					{
						flag=1;
						break;
					}
				if (flag==0)
					break;
			}
			printf("%s",ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd)));
		} else if (strstr(input, "dir"))
		{
			printf("%s", ftp_comm(ftp->cmd.sockfd, "LIST\r\n", strlen("LIST\r\n")));
			printf("%s\n", ftp_comm(ftp->data.sockfd, NULL, 0));
		} else if (strstr(input, "pwd"))
		{
			sprintf(cmd,"PWD\r\n");
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			printf("%s", response);
			sscanf(response,"%d", &command_code);
			if (command_code !=257)
			{
				printf("ERROR");
				return NULL;
			}
			sscanf(response,"%d \"%s \" ", &command_code,&data);
			data[strlen(data)-1] = '\0';
			printf("Thu muc hien hanh:%s\n",data);
		} else if (strstr(input, "nlist"))
		{
			printf("%s", ftp_comm(ftp->cmd.sockfd, "NLIST\r\n", strlen("NLIST\r\n")));
			printf("%s\n", ftp_comm(ftp->data.sockfd, NULL, 0));
		} else if (strstr(input, "bye"))
		{
			sprintf(cmd,"QUIT\r\n");
			printf("%s", ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd)));
			printf("FTP client will exit now");
			break;
		} else if (strstr(input, "cd"))
		{
			sprintf(cmd,"CWD %s\r\n",&st);
			response = ftp_comm(ftp->cmd.sockfd, cmd,strlen(cmd));
			printf("%s", response);
		} else if (strstr(input, "cdup"))
		{
			sprintf(cmd,"CDUP \r\n");
			response = ftp_comm(ftp->cmd.sockfd, cmd,strlen(cmd));
			printf("%s", response);
		} else if (strstr(input,"mkdir"))
		{
			sprintf(cmd,"MKD %s\r\n",st);
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			printf("%s", response);
		} else if(strstr(input, "delete"))
		{
			sprintf(cmd,"DELE %s\r\n",st);
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			printf("%s", response);
		//DOWNLOAD
		} else if(strstr(input,"download"))
		{
			char filename[BUFFSIZE_DATA];
			char *data;
			FILE *fo;
			fo = fopen(st,"w");
			sprintf(cmd,"RETR %s\r\n", st);
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			//printf("%s",response);
			data = ftp_comm(ftp->data.sockfd, NULL, 0);
			//printf("%s",data);
			printf("abc");
			while(1)
			{
				//fwrite(data,strlen(data)+1,1,fo);
				fputs(data,fo);
				int flag=0;
				printf("1");
				data = ftp_comm(ftp->data.sockfd, NULL, 0);
				
				response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
				printf("%s",response);
				for (int i=0;i<strlen(data);i++)
					if (data[i]!=0)
					{
						flag=1;
						break;
					}
				if (flag==0)
					break;
			}
			
			fclose(fo);
		//UPLOAD
		} else if(strstr(input,"upload"))
		{
			FILE *fin;
			char data[BUFFSIZE_DATA];
			char *response;
			fin = fopen(st,"rb");
			sprintf(cmd,"STOR %s\r\n",st);
			response = ftp_comm(ftp->cmd.sockfd, cmd, strlen(cmd));
			printf("%s",response);
			int flag;
			while ((flag = read(fin,data,BUFFSIZE_DATA))>0)
			{
				response = ftp_comm(ftp->data.sockfd,data,strlen(data));
				printf("%s",response);
			}
				
			fclose(fin);
		} else
		{
			printf("Khong ho tro lenh nay.\nMoi nhap lai.\n");
		}
	}
}
