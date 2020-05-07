
Compile the code using the provided makefile:

    make all


To clear the directory (and remove .txt files):
   
    make clean


Or make and run the Router/Master using the command:
    
    ./startup.sh ADDRESS

    - ADDRESS must be an IPv4 address in dot notation
    - If ADDRESS is 127.0.0.1, the server will start in routing mode
    - If starting a non-routing master/slave, use the address of the router
    - This will also start the monitoring slave
    - To stop this application without it rebooting itself, run the command 'ps -aux | grep tsd', 
      find the PID of tsds and tsdm, and run the command 'kill PID1 && kill PID2'
    - Alternatively, to test the rebooting functionality, a single process can be killed 
      using the 'kill PID' command


Run the client using the command:  

    ./tsc -r ADDRESS -u USERNAME

    - Address should be the address of the routing server
    - This process can be killed with Control-C or Control-Z

