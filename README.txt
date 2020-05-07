Compilation:

To compile the program, navigate to the root project directory in a bash terminal and type 'make' or 'make all'

Execution:

The server (tsd) should be running before the clients are started so the clients will be able to connect to the server.

1) In order to run the server, navigate to the root project directory in a bash shell and type the command './bin/tsd' after making the project.
   
2) To run the clients, first start up the server and then start the client with the command './bin/tsc [-h <HOST ADDRESS>][-p <PORT #>][-u <USERNAME>]' from the root project directory. The default hostname for the client is 'localhost' and the default port number is '3010'. The default username is 'default'. If a user with the same username has registered with the server since it has started, then the server will refuse the connection. Therefore, when using multiple clients simultaneously, different usernames must be chosen for each connected client.  
