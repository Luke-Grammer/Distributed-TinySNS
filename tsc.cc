#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <string>
#include <unistd.h>
#include <grpc++/grpc++.h>
#include "client.h"

#include "sns.grpc.pb.h"
using csce438::ListReply;
using csce438::Message;
using csce438::Reply;
using csce438::Request;
using csce438::SNSService;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using namespace std;

// Function to make a gRPC Message instance given a username and message string
Message MakeMessage(const string &username, const string &msg)
{
    // Create a message and set the username and message contents
    Message m;
    m.set_username(username);
    m.set_msg(msg);

    // Create a timestamp and populate it with the current time, then add it to the message
    google::protobuf::Timestamp *timestamp = new google::protobuf::Timestamp();
    timestamp->set_seconds(time(NULL));
    timestamp->set_nanos(0);
    m.set_allocated_timestamp(timestamp);

    // Return the message
    return m;
}

// Client class derives IClient
class Client : public IClient
{
public:
    Client(const string &raddr,
           const string &uname,
           const string &p)
        : router_addr(raddr), username(uname), port(p) {}

protected:
    virtual int connectTo();
    virtual IReply processCommand(string &input);
    virtual void processTimeline();

private:
    string router_addr = "";
    struct in_addr host_addr;
    string username = "";
    string port = "";
    bool connected = false;
    unique_ptr<SNSService::Stub> stub_;

    IReply Login();
    IReply List();
    IReply Follow(const string &username2);
    IReply Unfollow(const string &username2);
    void Timeline(const string &username);
};

int main(int argc, char **argv)
{
    string router_addr = "127.0.0.1";
    string username = "default";
    string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "r:u:p:")) != -1)
    {
        switch (opt)
        {
        case 'r':
            router_addr = optarg;
            break;
        case 'u':
            username = optarg;
            break;
        case 'p':
            port = optarg;
            break;
        default:
            cerr << "Invalid Command Line Argument\n";
        }
    }

    // Create new client instance with the given router address, username, and client port
    Client myc(router_addr, username, port);

    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

// Exit the process with a message in the event of a fatal error
void killSession(string error) 
{
	cerr << "\nCLIENT ERROR: " << error << endl;
	cerr << "errno: " << errno << endl;
	cerr << "Client encountered unrecoverable error!" << endl;
	cerr << "Client shutting down..." << endl;
	exit(EXIT_FAILURE);
}

// Connect to available master server if not already connected to available master
int Client::connectTo()
{   
    int sock;
    struct sockaddr_in addr;
    struct in_addr temp_host;
    char buf[1024];

    addr.sin_family = AF_INET;
    addr.sin_port = htons(stoi(port));

 	if((sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
		killSession("Socket error in connectTo()");
	
	// Convert router address from text to binary form and store in the struct
    if(inet_pton(AF_INET, router_addr.c_str(), &addr.sin_addr) <= 0)  
		killSession("Invalid router address in connectTo()");

	// Connect to the router 
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) 
		killSession("connect() to router failed in connectTo()");

    // Read the address of the available master into a temporary buffer  (router returns a single byte in the event that no master is available)
    int status;
    if ((status = read(sock, buf, 1024)) <= 0)
        killSession("read() failed in connectTo()");
    if (status == 1)
    {
        cout << "\nNo available masters for connection" << endl;
        return -1;
    }

    // Translate address into network structure and store in temp_host
    inet_pton(AF_INET, buf, &temp_host);

    // If temporary host address doesn't equal the current host address
    // set host address to temporary host address and do the following: 
    if (temp_host.s_addr != host_addr.s_addr)
    {
        string host_str(buf);
        
        // If this is not the first connection attempt, display reconnection message
        if(connected) 
            displayReConnectionMessage(host_str, port);

        string login_info = host_str + ":" + port;
        #ifdef DEBUG
            cout << "DEBUG: Attempting to connect to " << login_info << endl;
        #endif
        
        // Connect to the server and login
        stub_ = unique_ptr<SNSService::Stub>(SNSService::NewStub(
            grpc::CreateChannel(
                login_info, grpc::InsecureChannelCredentials())));

        IReply ire = Login();
        if (!ire.grpc_status.ok() || ire.comm_status != SUCCESS)
            return -1;
        
        host_addr = temp_host;
        connected = true;
    }

    // Else, do nothing (already connected to available master)
    return 1;
}

// Processed a given input command LIST/FOLLOW/UNFOLLOW/TIMELINE
IReply Client::processCommand(string &input)
{
    IReply ire;
    size_t index = input.find_first_of(" ");
    // Process commands with at least one argument (FOLLOW/UNFOLLOW)
    if (index != string::npos)
    {
        string cmd = input.substr(0, index);
        
        if (input.length() == index + 1)
            cout << "Invalid Input -- No Arguments Given\n";

        string argument = input.substr(index + 1, (input.length() - index));

        if (cmd == "FOLLOW")
            return Follow(argument);

        else if (cmd == "UNFOLLOW")
            return Unfollow(argument);
    }
    // Process commands with no arguments (LIST/TIMELINE)
    else
    {
        if (input == "LIST")
            return List();
        else if (input == "TIMELINE")
        {
            ire.comm_status = SUCCESS;
            return ire;
        }
    }

    ire.comm_status = FAILURE_INVALID;
    return ire;
}

// Enter the users timeline
void Client::processTimeline()
{
    Timeline(username);
}

// List all users, inclusing those following the current user
IReply Client::List()
{
    // Data being sent to the server
    Request request;
    request.set_username(username);

    // Container for the data from the server and current context
    ListReply list_reply;
    ClientContext context;

    Status status = stub_->List(&context, request, &list_reply);
    IReply ire;
    ire.grpc_status = status;

    // Loop through list_reply.all_users and list_reply.following_users
    // Print out the name of each room
    if (status.ok())
    {
        ire.comm_status = SUCCESS;
        string all_users;
        string following_users;
        for (string s : list_reply.all_users())
            ire.all_users.push_back(s);
        for (string s : list_reply.followers())
            ire.followers.push_back(s);
    }
    return ire;
}

// Follow a given user
IReply Client::Follow(const string &username2)
{
    // Data being sent to the server
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    // Container for the data from the server and current context
    Reply reply;
    ClientContext context;

    // Make the gRPC call and check the reply
    Status status = stub_->Follow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Follow Failed -- Invalid Username")
        ire.comm_status = FAILURE_INVALID_USERNAME;
    else if (reply.msg() == "Follow Failed -- Already Following User")
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    else if (reply.msg() == "Follow Successful")
        ire.comm_status = SUCCESS;
    else
        ire.comm_status = FAILURE_UNKNOWN;
    return ire;
}

// Unfollow a given user
IReply Client::Unfollow(const string &username2)
{
    // Data being sent to the server
    Request request;
    request.set_username(username);
    request.add_arguments(username2);

    // Container for the data from the server and current context
    Reply reply;
    ClientContext context;

    // Make the gRPC call and check the reply
    Status status = stub_->Unfollow(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Unfollow Failed -- Invalid Username")
        ire.comm_status = FAILURE_INVALID_USERNAME;
    else if (reply.msg() == "Unfollow Failed -- Not Following User")
        ire.comm_status = FAILURE_INVALID_USERNAME;
    else if (reply.msg() == "Unfollow Successful")
        ire.comm_status = SUCCESS;
    else
        ire.comm_status = FAILURE_UNKNOWN;
    return ire;
}

// Log in the current user
IReply Client::Login()
{
    // Data being sent to the server
    Request request;
    request.set_username(username);

    // Container for the data from the server and current context
    Reply reply;
    ClientContext context;

    // Make the gRPC call and check the reply
    Status status = stub_->Login(&context, request, &reply);
    IReply ire;
    ire.grpc_status = status;
    if (reply.msg() == "Invalid Username")
        ire.comm_status = FAILURE_ALREADY_EXISTS;
    else
        ire.comm_status = SUCCESS;

    //cout << reply.msg() << endl;
    return ire;
}

// Process the user's timeline, reconnecting to the available master whenever a connection is lost
void Client::Timeline(const string &username)
{
    // Thread shared variables
    static bool CONTINUE;
    static string LAST_MSG = "";

    while(true) 
    {  
        CONTINUE = true;
        ClientContext context;

        // Check if connected to current available master
        if (connectTo() < 0)
            killSession("Could not reconnect to available master");

        // Create bi-directional stream
        shared_ptr<ClientReaderWriter<Message, Message>> stream(
            stub_->Timeline(&context));

        //Thread used to read chat messages and send them to the server
        thread writer([username, stream]() {
            // Set the stream
            string input = "Set Stream";
            Message m = MakeMessage(username, input);
            if (!stream->Write(m))
            {
                // If the write fails, signal the reader and exit the thread
                #ifdef DEBUG
                    cout << "Writer: Stream has been closed on first write, signalling reader and reconnecting..." << endl;
                #endif
                CONTINUE = false;
                return 1;     
            }

            // If the writer was interrupted during the last run, try re-sending the last message the user provided
            if (LAST_MSG != "")
            {
                Message m = MakeMessage(username, LAST_MSG);
                if (!stream->Write(m))
                {
                    // If the write fails, signal the reader and exit the thread
                    #ifdef DEBUG
                        cout << "Writer: Stream has been closed on the re-write of the last message, signalling reader and reconnecting..." << endl;
                    #endif
                    CONTINUE = false;
                    return 1;     
                }
                LAST_MSG = "";
            }

            // While the writer has not been signalled by the reader  
            while (CONTINUE)
            {
                input = getPostMessage();
                m = MakeMessage(username, input);
                if (!stream->Write(m))
                {
                    // If the write fails, signal the reader and exit the thread, saving the last message the user wrote
                    LAST_MSG = input;
                    #ifdef DEBUG
                        cout << "Writer: Stream has been closed on subsequent write, signalling reader and reconnecting..." << endl;
                    #endif
                    CONTINUE = false;
                    return 1;
                }
            }        
            stream->WritesDone();
            return 0;
        });

        // Thread used to read messages from the server and print them for the client
        thread reader([username, stream]() {
            Message m;

            // Continue reading until the writer signals or the stream fails
            while (CONTINUE && stream->Read(&m))
            {
                google::protobuf::Timestamp temptime = m.timestamp();
                time_t time = temptime.seconds();
                displayPostMessage(m.username(), m.msg(), time);
            }
            // If the stream failed (writer did not signal)
            if (CONTINUE)
            {
                // Signal the writer and exit the thread
                CONTINUE = false;
                #ifdef DEBUG
                    cout << "Reader: Stream has been closed, reconnecting..." << endl;
                #endif  
            }
            // If the writer signalled, exit the thread
            else {
                #ifdef DEBUG
                    cout << "Reader: Received signal from writer, reconnecting..." << endl;
                #endif
            }
            
        });

        //Wait for the threads to finish
        writer.join();     
        reader.join();
        #ifdef DEBUG
            cout << "Reader and writer done, reconnecting..." << endl;
        #endif
    }
}
