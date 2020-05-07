if [ -z "$1"]
then 
	echo "Invalid arguments!"
	echo "Please provide the IPv4 address of the router or the loopback address to initialize the router"
	echo "format: ./startup.sh <ADDR>"
else

	clear
	make clean
	make all DEBUG=0
	clear

	if [[ $1 = "127.0.0.1" ]]
	then
		echo "Detected router settings."
		echo "local IPv4 address list for connecting to router:"
		ifconfig | grep "inet addr:"
	fi

	./tsdm -a $1 &
	sleep 5
	./tsds -a $1 &
fi
