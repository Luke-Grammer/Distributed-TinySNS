#include <ctime>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace std;

// Exit the process with a message in the event of a fatal error
void killSession(string error) 
{
	cerr << "\nSLAVE ERROR: " << error << endl;
	cerr << "errno: " << errno << endl;
	cerr << "Slave encountered unrecoverable error!" << endl;
	cerr << "Server shutting down..." << endl;
	exit(EXIT_FAILURE);
}

// Function to reap slave process on termination to avoid creating a defunct process
void reap(int signum) 
{
	wait(NULL);
}

// Function to maintain heartbeat message with slave server
void heartbeat(const char* router_addr, string client_port, string backend_port, string heartbeat_port) 
{
	int h_sock, b_sock;
	struct sockaddr_in h_addr, b_addr;
	const char* heartbeat_msg = "ALIVE";
	const char* dead_msg = "DEAD";
	char buf[1024] = {0};

	h_addr.sin_family = b_addr.sin_family = AF_INET;
	h_addr.sin_addr.s_addr = INADDR_ANY;
	h_addr.sin_port = htons(stoi(heartbeat_port));
	b_addr.sin_port = htons(stoi(backend_port));

	if((h_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0 || (b_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
		killSession("Socket error in heartbeat()");

	int opt = 1;
	if((setsockopt(h_sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &opt, sizeof(opt))) < 0)
		killSession("setsockopt() failed in heartbeat()");
    
	// Convert router address from text to binary form and store in the struct
    if(inet_pton(AF_INET, router_addr, &b_addr.sin_addr) <= 0)  
		killSession("Invalid router address in heartbeat()");

	// Convert master address from text to binary form and store in the struct
    if(inet_pton(AF_INET, "127.0.0.1", &h_addr.sin_addr) <= 0)  
		killSession("Invalid master address in heartbeat()");

	// Connect to the router 
	cout << "Slave connecting to router... ";
	if (connect(b_sock, (struct sockaddr *)&b_addr, sizeof(b_addr)) < 0) 
		killSession("connect() to router failed in heartbeat()");
	cout << "connected!" << endl;
	
	// Connect to the master 
	cout << "Slave connecting to master... ";
	if (connect(h_sock, (struct sockaddr *)&h_addr, sizeof(h_addr)) < 0) 
		killSession("connect() to master failed in heartbeat()");	
	cout << "connected!" << endl;
	
	// Set timeout on heartbeat reads to 5 seconds
	// This is the inactivity threshold for rebooting the master
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	if((setsockopt(h_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv))) < 0)
		killSession("setsockopt() failed in heartbeat()");
	
	cout << "Slave initialization complete, beginning keepalive." << endl;
	int status;
	while (true)
	{
		// Send heartbeat to master
    	send(h_sock, heartbeat_msg, strlen(heartbeat_msg), 0); 

		// Attempt to read heartbeat from master
    	status = read(h_sock, buf, 1024); 
		if (status <= 0)
		{
			#ifdef DEBUG
				cout << "SLV-DEBUG:  Failed to receive heartbeat message from master, restarting" << endl;
			#endif		

			// Disconnect from master
			close(h_sock);
			
			// Send message informing router of the masters death
			send(b_sock, dead_msg, strlen(dead_msg), 0);	
			
			// Close if still running and restart the master
			system("pkill -f tsdm");
			if(fork() == 0)
			{
				#ifdef DEBUG
					cout << "SLV-DEBUG:  Processed spawned to resurrect master" << endl;
				#endif
				const char* args[] = {"./tsdm", "-h", heartbeat_port.c_str(), "-c", client_port.c_str(), "-b", backend_port.c_str(), "-a", router_addr, NULL};
				execvp(args[0], (char**) args);
				killSession("exec() failure");
			} else // Set up signal handler
			{
				signal(SIGCHLD, reap);
			}

			// Wait for master to reboot
			sleep(2);

			if((h_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
				killSession("Socket error in heartbeat()");

			// Reconnect to master
			if (connect(h_sock, (struct sockaddr *)&h_addr, sizeof(h_addr)) < 0) 
				killSession("connect() to master failed in heartbeat()");				

			if((setsockopt(h_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv))) < 0)
				killSession("setsockopt() failed in heartbeat()");

			#ifdef DEBUG
				cout << "SLV-DEBUG:  Restarted master successfully" << endl;
			#endif
			continue;
		}

		// Don't flood master with heartbeat messages
		sleep(1);
	}
}

int main(int argc, char **argv)
{
	string client_port = "3010";
	string backend_port = "3059";
	string heartbeat_port = "3076";
	string router_address = "127.0.0.1";

	int opt = 0;
	while ((opt = getopt(argc, argv, "c:h:b:a:")) != -1)
	{
		switch (opt)
		{
		case 'c':
			client_port = optarg;
			break;
		case 'h':
			heartbeat_port = optarg;
			break;
		case 'b':
			backend_port = optarg;
			break;
		case 'a':
			router_address = optarg;
			break;
		default:
			cerr << "Invalid Command Line Argument\n";
			return -1;
		}
	}

	if (client_port == backend_port || client_port == heartbeat_port || heartbeat_port == backend_port)
		killSession("Invalid port selection, conflicting ports");

	// Start monitoring master server
	heartbeat(router_address.c_str(), client_port, backend_port, heartbeat_port);
	return 0;
}
