#moltengamepadctl

A (very basic) reference client for MoltenGamepad, using the socket interface introduced in MG version 1.0.0.

Supports sending commands to MG either through command line arguments or via standard input.

The `-i` command line flag effectively opens an interactive shell with the running instance of MG.

It is suggested that you create a wrapper script that specifies the location of your MG socket, so that you don't need to specify it everytime you wish to use this client.

    #!/bin/bash
    ./moltengamepadctl --socket-path /var/run/moltengamepad/mg.sock $@
