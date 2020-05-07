#include <sstream>
#include <unistd.h>
#include <thread>
#include <algorithm>
#include <grpc++/grpc++.h>
#include "client.h"

#include "ts.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

class Client : public IClient
{
    public:
	    Client(const std::string& hname,
               const std::string& uname,
               const std::string& p)
	    :hostname(hname), username(uname), port(p) {}
	
    protected:
        virtual int connectTo();
        virtual IReply processCommand(std::string& input);
        virtual void processTimeline();

    private:
        std::string hostname;
        std::string username;
        std::string port;
        
        // You can have an instance of the client stub
        // as a member variable.
    	std::unique_ptr<TSN::Stub> stub_;
};

int main(int argc, char** argv) {

    std::string hostname = "localhost";
    std::string username = "default";
    std::string port = "3010";
    int opt = 0;
    while ((opt = getopt(argc, argv, "h:u:p:")) != -1){
        switch(opt) {
        case 'h':
        	hostname = optarg;
		break;
        case 'u':
            username = optarg;
		break;
        case 'p':
            port = optarg;
		break;
        default:
            std::cerr << "Invalid Command Line Argument\n";
        }
    }

    Client myc(hostname, username, port);

    // You MUST invoke "run_client" function to start business logic
    myc.run_client();

    return 0;
}

// This function establishes a connection to the server
int Client::connectTo()
{
    // Create a client stub
    stub_ = TSN::NewStub(std::shared_ptr<Channel>(
    					 grpc::CreateChannel(hostname + ":" + port, 
    					 grpc::InsecureChannelCredentials())));
    
    // Initialize the request to send to the server
    UserRequest request;
    request.set_username(username);
    
    // Structure for the data we expect to get back from the server
    UserReply reply;

    // Context for the client.
    ClientContext context;

    // The actual RPC.
    Status status = stub_->AddUser(&context, request, &reply);

    // Act upon its status.
    if (status.ok() && (IStatus) reply.status() == IStatus::SUCCESS)
      	return 1;
	else
      	return -1;
}

// This function parses a given input command, sends the appropriate request to the server, 
// and stores the results in an IReply object
IReply Client::processCommand(std::string& input)
{
	IReply ire;
	
	ClientContext context;
	Status status;
	
	// Parse the first word of the command from the user
	std::stringstream ss(input);
    std::string command;
    ss >> command;
    
    // If the command was 'FOLLOW <USER>'
    if (command == "FOLLOW") {
    	// Initialize the request and reply objects
    	FollowUserRequest request;
    	UserReply reply;		
		request.set_username(username);
		
		// Get the user to follow
    	std::string userToFollow;
    	ss >> userToFollow;
    	request.set_user_to_follow(userToFollow);
    		
    	// Perform the RPC and get the result
    	status = stub_->FollowUser(&context, request, &reply);
    	if (status.ok()) {
    		ire.comm_status = (IStatus) reply.status();
    	}
    	else {
    		ire.comm_status = IStatus::FAILURE_UNKNOWN;
    	}
    }
    // If the command was 'UNFOLLOW <USER>'
    else if (command == "UNFOLLOW") {
    	// Initialize the request and reply objects
    	UnfollowUserRequest request;
    	UserReply reply;
    	request.set_username(username);	
    	
    	// Get the user to unfollow
    	std::string userToUnfollow;
    	ss >> userToUnfollow;
    	request.set_user_to_unfollow(userToUnfollow);
    	
    	// Perform the RPC and get the result
    	status = stub_->UnfollowUser(&context, request, &reply);
    	if (status.ok()) {
    		ire.comm_status = (IStatus) reply.status();
    	}
    	else {
    		ire.comm_status = IStatus::FAILURE_UNKNOWN;
    	}    	    
    }
    else if (command == "LIST") {
    	// Initialize the request and reply objects
		UserRequest request;
		ListUsersReply reply;
		request.set_username(username);
		
		// Perform the RPC and get the result
    	status = stub_->ListUsers(&context, request, &reply);
    	if (status.ok()) {
    		ire.comm_status = (IStatus) reply.status();
    	}
    	else {
    		ire.comm_status = FAILURE_UNKNOWN;
    	}
    	
    	// Populate the IReply object with the response strings from the RPC
    	std::stringstream all_users(reply.all_users());
    	std::string user;
    	
    	if (reply.all_users() != "") {
    		while (std::getline(all_users, user, '\n')) {
    			ire.all_users.push_back(user);
    		}
	    	std::sort(begin(ire.all_users), end(ire.all_users));
    	}
    	
    	std::stringstream followers(reply.followers());
    	
    	if (reply.followers() != "") {
    		while (std::getline(followers, user, '\n')) {
    			ire.followers.push_back(user);
    		}
    		std::sort(begin(ire.followers), end(ire.followers));
    	}
    }
    // If the command was 'TIMELINE'
    else if (command == "TIMELINE") {
    	ire.comm_status = SUCCESS;
    }
    // If the command was not recognized
    else {
    	ire.comm_status = FAILURE_UNKNOWN;
    }
    
    return ire;
}

// This function processes the 'TIMELINE' function and provides the user with the 
// ability to post to and read live updates from their timeline
void Client::processTimeline()
{
	// Create the client context and begin the bidirectional RPC stream
    ClientContext context;
    std::shared_ptr<ClientReaderWriter<PostMessage, PostMessage>> stream(stub_->ProcessTimeline(&context));
    
    // Create an initial message to send to the server containing the current user's username
    PostMessage userinfo;
    userinfo.set_content("");
    userinfo.set_time((long int) time(NULL));
    userinfo.set_sender(username);
    stream->Write(userinfo);
    	
 	// This thread constantly prompts the user for input and streams it to the server
   	std::thread writer{[stream](std::string username) {
        std::string msg;
       	while (1) {
       	    PostMessage p;
       	    p.set_content(getPostMessage());
       	    p.set_time((long int) time(NULL));
       	    p.set_sender(username);
       	    stream->Write(p);
       	}
       	stream->WritesDone();
    }, username};

	// This thread reads timeline updates from the server and prints them to standard output
   	std::thread reader([stream]() {
       	PostMessage p;
       	time_t time; 
       	while(stream->Read(&p)){
       	  	time = p.time();
       	    displayPostMessage(p.sender(), p.content(), time); 
       	}
   	});

   	//Wait for the threads to finish
   	writer.join();
   	reader.join();
}
