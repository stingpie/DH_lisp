Dollhouse is a scriptable p2p chat client. It is designed to be flexible to a ridiculous degree, to the point where you should be able to use it as a multiplayer game engine. It is super unfinished though. You can't really do anything right now. :/





### Design theory
Dollhouse is designed to encourage modular programming through automatically creating groups of mini-programs. Each mini-program serves a specific function, and is given a specific interface. Based on the function and interfaces, Dollhouse can select several mini-programs which would all waork together. 
This model allows Dollhouse to easily detect isomorphic structure on different machines, and combine them together when two peers connect (essentially creating a shared machine.)  


### Developer info:
There are:
1) Daemons:
    Daemons are processes with an environment, an info registry entry, and interfaces.
2) Info registry:
    Gives info about Daemons. The registry is loaded at start up, and information about interfaces specifically.
3) Interfaces:
    The input/ouput port of a Daemon. They are one directional, contain the name of the interface, the datatype and format of the data it will transmit.
4) Environments:
    Environments are the groups of variables which an interpreter uses while running. Each daemon has a seperate environment, which contains both data and code. 
5) Interlinks:
    Interlinks are set up during Daemon initialization. They take a source and destination and direct data flow between the two.


#### Internals:
Each language has a few functions for interacting with Dollhouse. yield() halts the program and returns execution to the main loop. output(interface, data) puts data into the environments output buffer, labels the buffer with the interface name, and yields to the main loop. In addition, there is a registerInterface(interface, function) function which registers a function to be executed when the interface is given data.
```
+----------+          INTERLINK         +----------+
|DAEMON 1  |        _____/\_____        |DAEMON 2  |
|          |____   /            \   ____|          |
|onInput() |___< <---------------- <____|output()  <--- function
|          |___                    _____|          |
|output()  |___> ----------------> >____|onInput() <--- function
+----------+                            +----------+
             /\                      /\
          Interface              Interface
```

The main loop goes like this:
```
1) start()
    +-- Create activeDaemonList, lispDaemons, and daemonInfoList.
    +-- For each ".proc" file, createDaemonRegistry(file)
    +-- startDaemon("main.lisp", "lisp")
        +-- For each interface, find CorrespondingInterface(interface)
            +-- registerInterlink(srcDaemon.interface, destDaemon.interface)

2) Cycle()
    +-- For each daemon, runDaemon(daemon)
    |   +-- for each daemon.interlink, cycleInterlink(daemon.interlink)
    +-- handle graphics, keyboard, signals, network.   

```









