# How to compile and run my server

## COMPILING
I have made a make file which will run the compilation for you. Simply write `$ make` in your terminal while in this directory and it will create assignment3.

## EXECUTING
Now to execute is really simple, I dont have any client programs as I use netcat to send my files to the server, so you will too! 
1. run `$ ./assignment3 -l *port* -p "*pattern*"`
    this will start the server in your terminal. The -l is listen flag and is followed by a port number you wish your server to listen on. -p is the pattern flag and is followed by any pattern you wish to anaylise in the books you will send. For example:
    `$ ./assignment3 -l 12345 -p "pattern"`
2. Open a new terminal, this new terminal will act as your client as you use nc commands to send files. To use netcat to send files use this command:
    `$ nc localhost *port* < file.txt`
    where localhost is used if you are running on a local machine (im hoping you are), and port is the port number from earlier. 

## Output

going back to the server terminal, you will have noticed that the server outputs messages informing you of 
1. Server connection
2. Lines added
3. Analysis threads searching for your pattern