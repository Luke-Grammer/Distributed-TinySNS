/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctime>

#include <google/protobuf/timestamp.pb.h>
#include <google/protobuf/duration.pb.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <memory>
#include <queue>
#include <string>
#include <stdlib.h>
#include <unistd.h>
#include <google/protobuf/util/time_util.h>
#include <grpc++/grpc++.h>

#include "sns.grpc.pb.h"

using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using google::protobuf::Duration;
using google::protobuf::Timestamp;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using namespace std; 

struct Client
{
	string username;
	bool connected = true;
	int following_file_size = 0;
	vector<Client *> client_followers;
	vector<Client *> client_following;
	ServerReaderWriter<Message, Message> *stream = 0;

	bool operator==(const Client &c1) const {
		return (username == c1.username);
	}
};

//Vector that stores every client that has been created
vector<Client> client_db;

// Exit the process with a message in the event of a fatal error
void killSession(string error) 
{
	cerr << "\nMASTER ERROR: " << error << " (errno " << errno << ")" << endl;
	cerr << "Master encountered unrecoverable error" << endl;
	cerr << "Server shutting down..." << endl;
	exit(EXIT_FAILURE);
}

//Helper function used to find a Client object given its username
int find_user(string username)
{
	int index = 0;
	for (Client c : client_db)
	{
		if (c.username == username)
			return index;
		index++;
	}
	return -1;
}

class SNSServiceImpl final : public SNSService::Service
{

	Status List(ServerContext *context, const Request *request, ListReply *list_reply) override
	{
		Client user = client_db[find_user(request->username())];
		for (Client c : client_db)
		{
			list_reply->add_all_users(c.username);
		}
		vector<Client *>::const_iterator it;
		for (it = user.client_followers.begin(); it != user.client_followers.end(); it++)
		{
			list_reply->add_followers((*it)->username);
		}
		return Status::OK;
	}

	Status Follow(ServerContext *context, const Request *request, Reply *reply) override
	{
		string username1 = request->username();
		string username2 = request->arguments(0);
		int join_index = find_user(username2);
		if (join_index < 0 || username1 == username2)
			reply->set_msg("Follow Failed -- Invalid Username");
		else
		{
			Client *user1 = &client_db[find_user(username1)];
			Client *user2 = &client_db[join_index];
			if (find(user1->client_following.begin(), user1->client_following.end(), user2) != user1->client_following.end())
			{
				reply->set_msg("Follow Failed -- Already Following User");
				return Status::OK;
			}
			user1->client_following.push_back(user2);
			user2->client_followers.push_back(user1);
			reply->set_msg("Follow Successful");
		}
		return Status::OK;
	}

	Status Unfollow(ServerContext *context, const Request *request, Reply *reply) override
	{
		string username1 = request->username();
		string username2 = request->arguments(0);
		int leave_index = find_user(username2);
		if (leave_index < 0 || username1 == username2)
			reply->set_msg("Unfollow Failed -- Invalid Username");
		else
		{
			Client *user1 = &client_db[find_user(username1)];
			Client *user2 = &client_db[leave_index];
			if (find(user1->client_following.begin(), user1->client_following.end(), user2) == user1->client_following.end())
			{
				reply->set_msg("Unfollow Failed -- Not Following User");
				return Status::OK;
			}
			user1->client_following.erase(find(user1->client_following.begin(), user1->client_following.end(), user2));
			user2->client_followers.erase(find(user2->client_followers.begin(), user2->client_followers.end(), user1));
			reply->set_msg("Unfollow Successful");
		}
		return Status::OK;
	}

	Status Login(ServerContext *context, const Request *request, Reply *reply) override
	{
		Client c;
		string username = request->username();
		int user_index = find_user(username);
		if (user_index < 0)
		{
			c.username = username;
			client_db.push_back(c);
			reply->set_msg("Login Successful!");
		}
		else
		{
			Client *user = &client_db[user_index];
			if (user->connected)
				reply->set_msg("Invalid Username");
			else
			{
				string msg = "Welcome Back " + user->username;
				reply->set_msg(msg);
				user->connected = true;
			}
		}
		return Status::OK;
	}

	Status Timeline(ServerContext *context,
					ServerReaderWriter<Message, Message> *stream) override
	{
		Message message;
		Client *c;
		while (stream->Read(&message))
		{
			string username = message.username();
			int user_index = find_user(username);
			if (user_index > -1)
			{
				#ifdef DEBUG
					cout << "Found user " << username << endl;
					cout << "(user index: " << user_index << ", max index: " << client_db.size() - 1 << ")" << endl;
				#endif
				c = &client_db[user_index];
			}
			else 
			{
				#ifdef DEBUG
					cout << "Could not find user " << username << "!" << endl;
				#endif		
				string ret_msg = "Username \"" + username + "\" not registered!";
				killSession(ret_msg);		
			}

			//Write the current message to "username.txt"
			string filename = username + ".txt";
			ofstream user_file(filename, ios::app | ios::out | ios::in);
			google::protobuf::Timestamp temptime = message.timestamp();
			string time = google::protobuf::util::TimeUtil::ToString(temptime);
			string fileinput = time + " :: " + message.username() + ":" + message.msg() + "\n";
			//"Set Stream" is the default message from the client to initialize the stream
			if (message.msg() != "Set Stream")
				user_file << fileinput;
			//If message = "Set Stream", print the first 20 chats from the people you follow
			else
			{
				//if (c->stream == 0)
					c->stream = stream;
				string line;
				vector<string> newest_twenty;
				ifstream in(username + "following.txt");
				if (in)
				{
					int count = 0;
					//Read the last up-to-20 lines (newest 20 messages) from userfollowing.txt
					while (getline(in, line))
					{
						if (c->following_file_size > 20)
						{
							if (count < c->following_file_size - 20)
							{
								count++;
								continue;
							}
						}
						newest_twenty.push_back(line);
					}
				
					Message new_msg;
					//Send the newest messages to the client to be displayed
					for (unsigned i = 0; i < newest_twenty.size(); i++)
					{
						new_msg.set_msg(newest_twenty[i]);
						stream->Write(new_msg);
					}
					continue;
				}
			}
			//Send the message to each follower's stream
			vector<Client *>::const_iterator it;
			for (it = c->client_followers.begin(); it != c->client_followers.end(); it++)
			{
				Client *temp_client = *it;
				if (temp_client->stream != 0 && temp_client->connected)
					temp_client->stream->Write(message);
				//For each of the current user's followers, put the message in their following.txt file
				string temp_username = temp_client->username;
				string temp_file = temp_username + "following.txt";
				ofstream following_file(temp_file, ios::app | ios::out | ios::in);
				following_file << fileinput;
				temp_client->following_file_size++;
				ofstream user_file(temp_username + ".txt", ios::app | ios::out | ios::in);
				user_file << fileinput;
			}
		}
		//If the client disconnected from Chat Mode, set connected to false
		c->connected = false;
		return Status::OK;
	}
};

// Function to register master server with router by sending the message 'MASTER'
void registerMaster(const char* router_addr, string backend_port)
{
	int sock;
	struct sockaddr_in addr;
	const char* register_msg = "MASTER";

	addr.sin_family = AF_INET;
	addr.sin_port = htons(stoi(backend_port));

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
		killSession("Socket error in registerMaster()");
	
	// Convert router address from text to binary form and store in the struct
    if(inet_pton(AF_INET, router_addr, &addr.sin_addr) <= 0)  
		killSession("Invalid router address in registerMaster()");

	// Connect to the router 
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
		killSession("connect() to router failed in registerMaster()");
	
	// Send message 'MASTER' to router
	send(sock, register_msg, strlen(register_msg), 0);
	close(sock);

	#ifdef DEBUG
		cout << "MSTR-DEBUG: Sent registration to router" << endl;
	#endif
}

// Function to reap slave process on termination to avoid creating a defunct process
void reap(int signum) 
{
	wait(NULL);
}

// Function to maintain heartbeat message with slave server
void heartbeat(const char* router_addr, string client_port, string backend_port, string heartbeat_port) 
{
	int h_sock, b_sock, slave;
	struct sockaddr_in h_addr, b_addr;
	int addr_len = sizeof(h_addr);
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
    
	#ifdef DEBUG
		cout << "MSTR-DEBUG: Router address: " << router_addr << endl;
	#endif

	// Convert router address from text to binary form and store in the struct
    if(inet_pton(AF_INET, router_addr, &b_addr.sin_addr) <= 0)  
		killSession("Invalid router address in heartbeat()");

	cout << "Master connecting to router... ";
	if (connect(b_sock, (struct sockaddr *)&b_addr, sizeof(b_addr)) < 0) 
		killSession("connect() to router failed in heartbeat()");
	cout << "connected!" << endl;
	
	// Start listening for heartbeats from slave
	if(bind(h_sock, (struct sockaddr*) &h_addr, sizeof(h_addr)) < 0)
		killSession("bind() failed in heartbeat()");

	if (listen(h_sock, 1) < 0) 
		killSession("listen() failed in heartbeat()");

	cout << "Master accepting connection from slave... ";
    if ((slave = accept(h_sock, (struct sockaddr *)&h_addr, (socklen_t*)&addr_len)) < 0) 
		killSession("accept() failed in heartbeat()");
	cout << "accepted!" << endl;

	// Set timeout on heartbeat reads to 5 seconds
	// This is the inactivity threshold for rebooting the slave
	struct timeval tv;
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	if((setsockopt(slave, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv))) < 0)
		killSession("setsockopt() failed in heartbeat()");
	
	cout << "Master initialization complete, beginning keepalive." << endl;
	int status;
	while (true)
	{
		// Send heartbeat to slave
    	send(slave, heartbeat_msg, strlen(heartbeat_msg), 0); 

		// Attempt to read heartbeat from slave
    	status = read(slave, buf, 1024); 
		if (status <= 0)
		{
			#ifdef DEBUG
				cout << "MSTR-DEBUG: Failed to receive heartbeat message from slave" << endl;
			#endif
			
			// Disconnect all clients
			for (Client client : client_db)
				client.connected = false;

			// Disconnect slave
			close(slave);	

			// Send message informing router of the slaves death
			// send(b_sock, dead_msg, strlen(dead_msg), 0);	

			// Close if still running and restart the slave via fork()/exec()
			system("pkill -f tsds");
			if(fork() == 0)
			{
				close(h_sock);
				close(b_sock);

				#ifdef DEBUG
					cout << "MSTR-DEBUG: Processed spawned to resurrect slave" << endl;
				#endif
				const char* args[] = {"./tsds", "-h", heartbeat_port.c_str(), "-c", client_port.c_str(), "-b", backend_port.c_str(), "-a", router_addr, NULL};
				execvp(args[0], (char**) args);
				killSession("exec() failure");
			} else // Set up signal handler
			{
				signal(SIGCHLD, reap);
			}
			
			// Wait for slave to reboot
			sleep(2);

			// Wait for slave to re-connect
			if ((slave = accept(h_sock, (struct sockaddr *)&h_addr, (socklen_t*)&addr_len)) < 0) 
				killSession("accept() failed in heartbeat()");
			cout << "Accepted slave reconnection" << endl;

			if((setsockopt(slave, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv))) < 0)
				killSession("setsockopt() failed in heartbeat()");

			#ifdef DEBUG
				cout << "MSTR-DEBUG: Restarted slave successfully, re-registering with router" << endl;
			#endif
			
			// Re-register with router
			registerMaster(router_addr, backend_port);
			continue;
		}

		// Don't flood slave with heartbeat messages
		sleep(1);
	}
}

// Function to route clients to available registered master servers and manage available masters
void route(string client_port, string backend_port) 
{
	vector<struct in_addr> hierarchy;
	vector<int> servers;
	vector<struct sockaddr_in> server_addrs;
	char buf[1024];

	int b_sock, c_sock;
	struct sockaddr_in b_addr, c_addr;
	int addr_len = sizeof(b_addr);

	c_addr.sin_family = b_addr.sin_family = AF_INET;
	c_addr.sin_addr.s_addr = b_addr.sin_addr.s_addr = INADDR_ANY;
	c_addr.sin_port = htons(stoi(client_port));
	b_addr.sin_port = htons(stoi(backend_port));

	// Create server socket on client and backend ports
	if((b_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0 || (c_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
		killSession("Socket error in route()");
	
	int opt = 1;
	if((setsockopt(b_sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &opt, sizeof(opt))) < 0)
		killSession("setsockopt() failed in route()");

	if((setsockopt(c_sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &opt, sizeof(opt))) < 0)
		killSession("setsockopt() failed in route()");

	if((bind(b_sock, (struct sockaddr*) &b_addr, sizeof(b_addr)) < 0) || (bind(c_sock, (struct sockaddr*) &c_addr, sizeof(c_addr)) < 0))
		killSession("Could not successfully bind sockets in route()");

	// Listen for connection requests on backend (for masters/slaves) and client (for clients) ports
	if ((listen(b_sock, 128) < 0) || (listen(c_sock, 128) < 0)) 
		killSession("listen() failed in route()");
	
	fd_set readfds;
	while(true)
	{
		int maxfd = max(b_sock, c_sock);
		FD_ZERO(&readfds);
		FD_SET(b_sock, &readfds);
		FD_SET(c_sock, &readfds);

		for (auto sock : servers) 
		{
			if (sock > maxfd) maxfd = sock;
			FD_SET(sock, &readfds);
		}

		if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) 
            killSession("select failed in route()");


		// Accept connection requests from masters/slaves
		if (FD_ISSET(b_sock, &readfds)) 
		{
			int temp = accept(b_sock, (struct sockaddr*)&b_addr, (socklen_t*)&addr_len);
			if (temp < 0) 
				killSession("accept() failed in route()");
			else
			{
				// Listen for future communication from newly connected server
				servers.push_back(temp);
				server_addrs.push_back(b_addr);
			}
		}

		// Check among connected servers for new messages
		for(unsigned i = 0; i < servers.size(); i++)
		{
			if (FD_ISSET(servers[i], &readfds)) 
			{
				// Read the new message
				int status = read(servers[i], buf, 1024); 
				if (status < 0)
					killSession("read() failed in route()");
				else if (status == 0) // Disconnection
				{
					close(servers[i]);
					servers.erase(servers.begin() + i);
					server_addrs.erase(server_addrs.begin() + i);
					i--;
					continue;
				}
				else if (buf[0] == 'M') // Register master
				{
					// Add ipv4 of servers[i] to the bottom of the hierarchy of available masters
					hierarchy.push_back(server_addrs.at(i).sin_addr);
					#ifdef DEBUG
						cout << "RTR-DEBUG:  Registered master #" << hierarchy.size() << endl;
					#endif
				}
				else if (buf[0] == 'D') // Reporting dead master/slave
				{
					#ifdef DEBUG
						cout << "RTR-DEBUG:  About to remove master, pool size: " << hierarchy.size() << endl;
					#endif
					// Remove server from the hierarchy of available masters
					for (int j = hierarchy.size() - 1; j >= 0; j--)
					{
						if (hierarchy.at(j).s_addr == server_addrs.at(i).sin_addr.s_addr)
							hierarchy.erase(hierarchy.begin() + j);
					}
					#ifdef DEBUG
						cout << "RTR-DEBUG:  Removed master, new pool size: " << hierarchy.size() << endl;
					#endif
				} 
				else
				{
					#ifdef DEBUG
						cout << "RTR-DEBUG:  Unknown router request: ";
						printf("%.*s\n", status, buf);
					#endif
				}
			}
		}

		// If a new client connects
		if (FD_ISSET(c_sock, &readfds)) 
		{
			#ifdef DEBUG
				cout << "RTR-DEBUG:  Client socket set" << endl;
			#endif

			// Accept the client connection
			int temp = accept(c_sock, (struct sockaddr*)&c_addr, (socklen_t*)&addr_len);
			if (temp < 0) 
				killSession("accept() failed in route()");
			
			// If there are masters available
			if (hierarchy.size() != 0)
			{
				char ip[INET_ADDRSTRLEN];

				// Convert the available master's address to string format
				if (inet_ntop(AF_INET, &hierarchy.at(0), ip, INET_ADDRSTRLEN) <= 0)
					killSession("Failed to convert address to string in route()");

				// Send the client the address of the available master
				send(temp, ip, INET_ADDRSTRLEN, 0);
				#ifdef DEBUG
					cout << "RTR-DEBUG:  Directed client to available master" << endl;
				#endif
			}
			// If there are no available masters, send a single byte
			else 
			{
				send(temp, "0", 1, 0);
				#ifdef DEBUG
					cout << "RTR-DEBUG:  No masters available, could not direct client to available master" << endl;
				#endif				
			}

			// Close the client connection
			close(temp);	
		}
	}
}

// Run the gRPC client server
void runServer(string client_port)
{
	string server_address = "0.0.0.0:" + client_port;
	SNSServiceImpl service;

	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	unique_ptr<Server> server(builder.BuildAndStart());
	cout << "Server listening for client requests on " << server_address << endl;

	server->Wait();
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

	// Cannot operate when ports collide
	if (client_port == backend_port || client_port == heartbeat_port || heartbeat_port == backend_port)
		killSession("Invalid port selection, conflicting ports");

	// Start heartbeat thread to monitor slave
	thread monitor(heartbeat, router_address.c_str(), client_port, backend_port, heartbeat_port);

	// If the server will operate as a router, route().
	if (router_address == "127.0.0.1")
		route(client_port, backend_port);
	// Otherwise, register with router and run the client server
	else
	{
		registerMaster(router_address.c_str(), backend_port);
		runServer(client_port);
	}

	monitor.join();
	return 0;
}

