# C-Plus-Test-Task
Purpose: Create a client-server application for Windows.

## Algorithm: 

• Clients send to the server random integer numbers from 0 to 1023.

• Server receives and stores these numbers into a container as unique values.

• Upon a client's request, the average of squares of the numbers including the newly received number is calculated and sent back to the client as a response.

• A client receives this value, generates a new random number and then again sends it to the server and so on.

• Every N seconds the server makes a dump - it stores all numbers from the container into a file in the binary format

## Details: 

• User has to be able to normally stop clients and server (for example, using ESC button)

• Server has to make a dump in the separate thread - not in the thread(s) which is used for communication with clients

• Output all the processes into console and the application's log file

• When developing, you must use the Boost Asio library 

## Notes: 

• Consider to develop this task as a production-like code. We will pay attention not only on code correctness but also on code quality

• For tests purposes run 1 server and about 10 clients. Keep them working for 10 -20 minutes.

### Assembly notes

• To Compile, install latest Boost as NuGet Package
