#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <netdb.h> /* addrinfo */

#include <numeric> /* accumulate */
#include <set>
#include <list>
#include <map>
#include <unordered_map>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

#include "select.hpp"
#include "node.hpp"

/* HEADERS:

~   ~   ~   ~   ~   ~   ~   ~   ~ ACK HEADER ~   ~   ~   ~   ~   ~   ~   ~   ~
 _________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                     PAYLOAD                    |
|-------------------------------------------------------------------------------------------------|
|                                                | LAST MSG ID | FUNC ID TO ACK TO |              |
|_________________________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~   ~ RELAY ACK HEADER ~   ~   ~   ~   ~   ~   ~   ~   ~
 _______________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                        PAYLOAD                       |
|-------------------------------------------------------------------------------------------------------|
|                                                | LAST MSG ID | FUNC ID TO ACK TO | GENERAL REQUEST ID |
|_______________________________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~   ~ NACK HEADER ~   ~   ~   ~   ~   ~   ~   ~   ~
 _________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                     PAYLOAD                    |
|-------------------------------------------------------------------------------------------------|
|                                                | LAST MSG ID | FUNC ID TO NACK TO |             |
|_________________________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~   ~ ROUTE HEADER ~   ~   ~   ~   ~   ~   ~   ~   ~
 ____________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                     PAYLOAD                       |
|----------------------------------------------------------------------------------------------------|
|                                                | FIRST GENERAL REQUEST ID | PATH LEN | PATH . . .  |
|____________________________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~   ~ RELAY HEADER ~   ~   ~   ~   ~   ~   ~   ~   ~
 ______________________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                          PAYLOAD                            |
|--------------------------------------------------------------------------------------------------------------|
|                                                | NEXT NODE ID | NUM OF MESSAGES TO SEND | GENERAL REQUEST ID |
|______________________________________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~   DISCOVER HEADER   ~   ~   ~   ~   ~   ~   ~   ~
 _______________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |               PAYLOAD                |
|---------------------------------------------------------------------------------------|
|                                                | DEST | GENERAL REQUEST ID |          |
|_______________________________________________________________________________________|

~   ~   ~   ~   ~   ~   ~   ~ DISCOVER NACK HEADER ~   ~   ~   ~   ~   ~   ~   ~
 _______________________________________________________________________________________________________________
| MSG ID | SRC ID | DST ID | TRAIL MSG | FUNC ID |                           PAYLOAD                            |
|---------------------------------------------------------------------------------------------------------------|
|                                                | LAST MSG ID | FUNC ID TO NACK TO | GENERAL REQUEST ID | DEST |
|_______________________________________________________________________________________________________________|

*/

/* my id */
int id;
/* key - neighbor id. value - socket. */
unordered_map<int,unsigned int> sockets; // const unsigned int
/* Note! to get the neighbors we can iterate thru this map keys. */

unordered_map<int,string> text;

/* save path to each node */
unordered_map<int,vector<int>> waze;
/* storing the nodes left to the router from this node (usable by methods like discover method) */
unordered_map<int,set<int>> nodes_to_request;
/* save the source first neighbors */
unordered_map<int,set<int>> first_src_neis;
/* saves the data of the node to which an answer should be returned.
   key: source id (e.g general_request_id). 
   value: pair (first - the node id to reply. second - last message id) */
unordered_map<int,pair<int, int>> node_to_reply;

//message * main_msg;
int main(int argc, char *argv[]) {
    int innerfd = 0, outerfd=0;
    struct sockaddr_in serv_addr; 
    int ret, i;
    int r_port;
    printf("Please choose a port: ");
    scanf("%d", &r_port);
    char buff[1025];
    // time_t ticks;
    innerfd = socket(AF_INET, SOCK_STREAM, 0);

    int enable = 1;
    if (setsockopt(innerfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        printf("\nInvalid address/ Address not supported \n");
        return 1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(r_port);
    bind(innerfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    printf("adding fd(%d) to monitoring\n", innerfd);
    add_fd_to_monitoring(innerfd);
    listen(innerfd, 10);
    printf("---------------------------------\n");
    cout << "USE THE COMMAND: connect,192.168.190.129:12350" << endl;
    /* Print my ip */
    struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&inet_addr;
    struct in_addr ipAddr = pV4Addr->sin_addr;
    char str[INET_ADDRSTRLEN];
    inet_ntop( AF_INET, &ipAddr, str, INET_ADDRSTRLEN );
    printf("MY IP: %s\n", str);
    /* Print my port */
    printf("MY PORTS: %d\n", r_port);
    printf("---------------------------------\n");

    while(true){
        memset(&buff, '\0', sizeof(buff));
	    printf("waiting for input...\n");
	    ret = wait_for_input();
	    printf("fd: %d is ready. reading...\n", ret);
        /* At the file descriptor level, stdin is defined to be file descriptor 0,
           stdout is defined to be file descriptor 1,
           and stderr is defined to be file descriptor 2. */
        if (ret < 2) { // std input
            read(ret, buff, 1025);
            if (buff[strlen(buff)-1]=='\n') buff[strlen(buff)-1] = '\0';
            stringstream ss;
            ss << buff;
            string splited[4]; // split std input
            getline(ss,splited[0],',');
            if (splited[0].compare("setid")==0) {std_setid(ss,splited);}
            else if (splited[0].compare("connect")==0) {std_connect(ss,splited);}
            else if (splited[0].compare("send")==0) {std_send(ss,splited);}
            else if (splited[0].compare("route")==0) {std_route(ss,splited);}
            else if (splited[0].compare("peers")==0) {std_peers(ss,splited);}
        } else { /* we got a packet */
            socklen_t addrlen;
            addrlen = sizeof(serv_addr);
            if (ret==innerfd) { /* if ret==innerfd then we got a new connection! */
                if ((ret = accept(ret, (struct sockaddr *)&serv_addr, &addrlen))<0) {
                    perror("accept");
                    exit(EXIT_FAILURE);
                }
            }
            /* get the message to struct */
            message* incoming = (message*)malloc(sizeof(message));
            int bytes_readed = read(ret ,incoming, 512);
            /* if 0 bytes were called from the socket. The socket has disconnected! */
            if (bytes_readed==0) {
                /* remove the socket from "sockets"! */
                for(auto it = sockets.begin(); it != sockets.end(); it++) {
	                if((it->second) == ret) {
		                sockets.erase(it->first);
		                break;}
                }
                cout << "socket " << ret << " removed" << endl;
                /* remove the socket from monitoring! */
                remove_fd_from_monitoring(ret);
                continue;
            }
            /* print message type */
            cout << "\033[1;36m"; /* print in color */
            cout << "Got " << message_type(incoming) << " message type!" << endl;
            cout << "\033[0m"; /* end print in color */
            /* handle the message */
            gotmsg(incoming, ret);
            delete(incoming);
        }
	}
}

////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// STD INPUT METHODS /////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

/* ----------------------------- SETID (STDIN) ---------------------------------- */
void std_setid(stringstream& ss,string splited[]) {
    getline(ss,splited[1],','); /* id */
    id = stoi(splited[1]);
    cout << "\033[1;36m"; /* print in color */
    cout << "MY ID: " << id << endl;
    cout << "\033[0m"; /* end print in color */
}

/* ---------------------------- CONNECT (STDIN) --------------------------------- */
void std_connect(stringstream& ss,string splited[]) {
    getline(ss,splited[1],':'); /* ip */
    getline(ss,splited[2],':'); /* port */
    uint16_t port = stoul(splited[2]);
    char const* destip = splited[1].c_str();
    struct sockaddr_in destAddress;
    int new_sock = socket(AF_INET, SOCK_STREAM, 0);
    /* set reuse option to enable */
    int enable = 1;
    if (setsockopt(new_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        printf("\nInvalid address/ Address not supported \n");
        return;
    }
    /* bind socket to destAddress */
    bind(new_sock, (struct sockaddr*)&destAddress, sizeof(destAddress));
    memset(&destAddress, 0, sizeof(destAddress));
	destAddress.sin_family = AF_INET;
    destAddress.sin_port = htons(port);
    if(inet_pton(AF_INET, destip, &destAddress.sin_addr)<=0) {
        printf("\nInvalid address/ Address not supported \n");
        return;}
    if (connect(new_sock, (struct sockaddr*)&destAddress, sizeof(destAddress)) < 0) {
        printf("\nConnection Failed \n");
        return;}
    add_fd_to_monitoring(new_sock);
    message outgoing;
    outgoing.id = rand();
    outgoing.src = id;
    outgoing.dest = 0;
    outgoing.trailMSG = 0;
    outgoing.funcID = 4; /* connect function id is 4 */
    char outgoing_buffer[512];
    memcpy(outgoing_buffer, &outgoing, sizeof(outgoing));
    send(new_sock, outgoing_buffer, sizeof(outgoing_buffer), 0);
}

/* ------------------------------ SEND (STDIN) ---------------------------------- */
void std_send(stringstream& ss,string splited[]) {
    getline(ss,splited[1],','); /* destination */
    getline(ss,splited[2],','); /* message length */
    getline(ss,splited[3],','); /* message itself */
    if (sockets.size()==0) { /* No neighbors at all!. TODO: send to myself */
        cout << "NACK" << endl;
        return;
    }
    if (sockets.find(stoi(splited[1]))!=sockets.end()) { /* connected directly. send the message! */
        message outgoing;
        outgoing.id = rand();
        outgoing.src = id;
        outgoing.dest = stoi(splited[1]);
        outgoing.trailMSG = 0;
        outgoing.funcID = 32; /* send function id is 32 */
        int msg_len = stoi(splited[2]);
        memcpy(outgoing.payload, &msg_len, sizeof(int));
        memcpy(outgoing.payload+sizeof(int), splited[3].c_str(), msg_len); /* set the payload */
        char outgoing_buffer[512];
        memcpy(outgoing_buffer, &outgoing, sizeof(outgoing));
        send(sockets.at(outgoing.dest), outgoing_buffer, sizeof(outgoing_buffer), 0);
    } else { /* not connected directly. we need to discover/relay&send. */
        text[stoi(splited[1])] = splited[3];
        if(waze.find(stoi(splited[1]))==waze.end()){ /* no path! lets discover */
            int original_id = rand();
            node_to_reply[original_id] = {-1, -1}; /* source node! stop condition */
            for(auto nei : sockets) {
                cout << "inserting " << nei.first << " to first_src_neis list.." << endl;
                first_src_neis[stoi(splited[1])].insert(nei.first);
            }
            send_discover(stoi(splited[1]),original_id);}
        else { /* if there is already a path */
            int original_id = rand();
            node_to_reply[original_id] = {-1, -1}; /* source node! stop condition */
            send_relay(stoi(splited[1]),original_id);
        }
    }
}

/* ------------------------------ ROUTE (STDIN) --------------------------------- */
void std_route(stringstream& ss,string splited[]) {
    getline(ss,splited[1],','); /* destination */
    if (stoi(splited[1])==id) { /* This is the current node! */
        cout << id << endl;
    }
    if (sockets.find(stoi(splited[1]))!=sockets.end()) { /* This is a neighbor! */
        cout << id << "->" << stoi(splited[1]) << endl;
        return;
    }
    if (sockets.size()==0) { /* No neighbors at all! */
        cout << "NACK" << endl;
        return;
    }
    waze.erase(stoi(splited[1]));
    int original_id = rand();
    node_to_reply[original_id] = {-1, -1}; /* source node! stop condition */
    for(auto nei : sockets) {
        cout << "inserting " << nei.first << " to first_src_neis list.." << endl;
        first_src_neis[stoi(splited[1])].insert(nei.first);
    }
    send_discover(stoi(splited[1]),original_id);
}

/* ------------------------------ PEERS (STDIN) --------------------------------- */
void std_peers(stringstream& ss,string splited[]) {
    cout << "ack" << endl;
    if (sockets.size()==0) {return;} /* no neighbors at all! */
    auto it = sockets.begin();
    cout << it->first;
    it++;
    for(; it != sockets.end(); it++) {
        cout << "," << it->first;
    }
    cout << endl;
}

////////////////////////////////////////////////////////////////////////////////////
///////////////////////////// EXTERNAL SOCKETS METHODS /////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

void gotmsg(message* msg, int ret){
    switch(msg->funcID){
        case 1:{ /* ACK */
            ack(msg,ret);
            break;}
        case 2:{ /* NACK */
            nack(msg, ret);
            break;}
       case 4:{ /* CONNECT */
           cnct(msg, ret);
           break;}
        case 8:{ /* DISCOVER */
            discover(msg, ret);
            break;}
        case 16:{ /* ROUTE */
            route(msg, ret);
            break;}
        case 32:{ /* SEND */
            Send(msg, ret);
            break;}
        case 64: { /* RELAY */
            relay(msg, ret);
            break;}
    }
}

/* ----------------------------------- ACK -------------------------------------- */
void ack(message* msg, int ret){//,int messagenum){
    int ack_type;
    memcpy(&ack_type, msg->payload+sizeof(int), sizeof(int)); /* save ack_type from msg payload */
    /* if the socket is unknown then the ack is for a connect message!
       therfore add the socket to connected sockets list! */
    if (ack_type==4) { /* respond to a connect message */
        sockets[msg->src] = ret;
        //sockets.insert({msg->src,ret});
    }
}

/* ---------------------------------- NACK -------------------------------------- */
void nack(message* msg, int ret) {
    /* nack type is the function id that the nack returns for */
    int nack_type;
    memcpy(&nack_type, msg->payload+sizeof(int), sizeof(int)); /* save nack_type from msg payload */
    if (nack_type==8) { /* got nack for discover function! */
        int general_request_id;
        int destination;
        memcpy(&general_request_id, msg->payload+2*sizeof(int), sizeof(int)); /* save general_request_id from msg payload */
        memcpy(&destination, msg->payload+3*sizeof(int), sizeof(int)); /* save destination from msg payload */
        if (node_to_reply[general_request_id].first==-1) { /* were on source node! */
            first_src_neis[destination].erase(msg->src); /* remove the current node from neighbors */
            if (first_src_neis[destination].size()>0) { /* there are more neighbors. keep discovering! */
                send_discover(destination,general_request_id);
                return;
            } else { /* returned to source node. finish! */
                if (waze.count(destination)!=0) { /* found path! */
                    /* there is text to send so discover called by relay! call relay! */
                    if (!text[destination].empty()) {
                        send_relay(destination,general_request_id);
                    } else { /* discover called by route! print the path! */
                        int len = waze[destination].size();
                        cout << "path: " << id << "->";
                        for (int i = 0; i < len-1; i++) {
                            cout << waze[destination][i] << "->";
                        }
                        cout << waze[destination][len-1] << endl;
                    }
                } else { /* didnt found path */
                    cout << "sorry, didnt find path! :(" << endl;
                }
            }
        } else { /* were on inner node! */
            nodes_to_request[general_request_id].erase(msg->src); /* remove the current node from neighbors */
            if (nodes_to_request[general_request_id].size()>0) { /* there are more neighbors. keep discovering! */
                send_discover(destination,general_request_id);
                return;
            } else { /* done searching! */
                /* lets check if we found path */
                if (waze.count(general_request_id)!=0) { /* found path! */
                    cout << "done searching! should return route!" << endl;
                    send_route(msg);
                } else { /* done searching! but didnt find path :( */
                    cout << "done searching! should return nack!" << endl;
                    send_nack(msg);
                }
            }
        }
    } else if (nack_type==64) { /* TODO: got nack for relay function! */

    }
}

/* --------------------------------- CONNECT ------------------------------------ */
void cnct(message* msg, int ret){
    message rply; /* ack */
    rply.id=random();
    rply.src=id;
    int connect_id = 4;
    memcpy(rply.payload, (char*)&msg->id,sizeof(int));
    memcpy(rply.payload+sizeof(int), &connect_id,sizeof(int));
    rply.trailMSG=0;
    rply.funcID=1;
    sockets[msg->src] = ret;
    //sockets.insert({msg->src,ret});
    add_fd_to_monitoring(ret);
    write(ret,&rply,sizeof(rply));
}

/* ---------------------------------- SEND -------------------------------------- */
void Send(message* msg, int ret) {
    int length;
    memcpy(&length, &msg->payload, sizeof(int));
    string newString;
    newString.resize(length);
    memcpy((char*)newString.data(), msg->payload+sizeof(int), length);
    //memcpy(&text, msg->payload, length);
    cout << "getting len: " << length << ". msg: " << newString << endl;
    message rply; /* ack */
    rply.id=random();
    rply.src=id;
    rply.dest=msg->src;
    memcpy(rply.payload, (char*)&msg->id,sizeof(int));
    rply.trailMSG=0;
    rply.funcID=1;
    write(ret,&rply,sizeof(rply));
}

/* -------------------------------- DISCOVER ------------------------------------ */
void discover(message* msg, int ret) {
    /* Note! If discover method was activated so dest is not an neighbor! */
    int destination;
    int general_request_id;
    memcpy(&destination, msg->payload, sizeof(int)); /* save destination from msg payload */
    memcpy(&general_request_id, msg->payload+sizeof(int), sizeof(int)); /* save general_request_id from msg payload */
    /* if we got discover message so the current path could be wrong! erase the path if exists */
    waze.erase(general_request_id);
    /* circle! cannot continue discovering! */
    if ((node_to_reply[general_request_id].first!=-1&&nodes_to_request[general_request_id].size()>0)
            ||(node_to_reply[general_request_id].first==-1&&first_src_neis[destination].size()>0)) {
        cout <<  msg->src <<" send to me (my id:" << id << ") and closed circle! return nack" << endl;
        send_nack(msg);
        return;
    }
    /* update node_to_reply (overwrite if exists) */
    node_to_reply[general_request_id] = {msg->src,msg->id};
    if (sockets.find(destination)!=sockets.end()) { /* if destination is a neighbor we found the node! */
        cout << "found the socket " << destination << "! return route" << endl;
        send_route(msg);
    } else if (sockets.size()==1) { /* leaf! cannot continue discovering! */
        cout << "i am a leaf! return nack!" << endl;
        send_nack(msg);
    } else { /* we can continue discovering */
        cout << "continue discovering" << endl;
        /* add all neighbors to nodes_to_request & keep discovering forward (to random neighbor) */
        for(auto nei : sockets) { 
            /* if a node sent us discover message we would not want
               to return to this node a discover message */
            if (nei.first!=msg->src) {
                nodes_to_request[general_request_id].insert(nei.first);
                cout << "adding " << nei.first << " to my nodes_to_request" << endl;
            }
        }
        send_discover(destination,general_request_id);
    }
}

/* --------------------------------- ROUTE -------------------------------------- */
void route(message* msg, int ret) {
    // TODO: add all routes to current node
    int length;
    memcpy(&length, msg->payload+(1)*sizeof(int), sizeof(int)); /* save path length from msg payload */
    vector<int> way;
    for (int i = 0; i < length; i++) {
        int element;
        memcpy(&element, msg->payload+(2+i)*sizeof(int), sizeof(int));
        way.push_back(element);
    }
    int destination = way[length-1];
    if(waze.count(destination)==0||length<waze[destination].size()){
        waze[destination].clear();
        for(int i=0;i<length;i++){
            waze[destination].push_back(way[i]);
        }
    } else if (length==waze[destination].size()) { // there is a path
        if (accumulate(way.begin(),way.end(),0)<accumulate(waze[destination].begin(),waze[destination].end(), 0)) {
            waze[destination].clear();
            for (int i = 0; i < length; i++) {
                waze[destination].push_back(way[i]);
            }
        }
    }
    int general_request_id;
    memcpy(&general_request_id, msg->payload, sizeof(int));
    if (node_to_reply[general_request_id].first==-1) { /* were on source node! */
        cout << "src node.." << endl;
        first_src_neis[destination].erase(msg->src); /* remove the current node from neighbors */
        if (first_src_neis[destination].size()>0) { /* there are more neighbors. keep discovering! */
            send_discover(destination,general_request_id);
            return;
        } else { /* we visited all the nodes & returned to source node. finish! */
            if (waze.count(destination)!=0) { /* found path! */
                /* there is text to send so discover called by relay! call relay! */
                if (!text[destination].empty()) {
                    send_relay(destination,general_request_id);
                } else { /* discover called by route! print the path! */
                    int len = waze[destination].size();
                    cout << "path: " << id << "->";
                    for (int i = 0; i < len-1; i++) {
                        cout << waze[destination][i] << "->";
                    }
                    cout << waze[destination][len-1] << endl;
                }
            } else { /* didnt found path */
                cout << "sorry, didnt found path! :(" << endl;
            }
            //node_to_reply.erase(general_request_id);
            return;
        }
    } else { /* were on inner node! */
        cout << "inner node.." << endl;
        nodes_to_request[general_request_id].erase(msg->src); /* remove the current node from neighbors */
        if (nodes_to_request[general_request_id].size()>0) { /* there are more neighbors. keep discovering! */
            send_discover(destination,general_request_id);
            return;
        } else { /* should return route to prev node */
            cout << "sending route.." << endl;
            send_route(msg);
        }
    }
}

/* --------------------------------- RELAY -------------------------------------- */
void relay(message* msg, int ret){
    int trail,dest,src, general_request_id;
    memcpy(&trail, &msg->trailMSG, sizeof(int));
    memcpy(&dest, &msg->payload,sizeof(int));
    memcpy(&general_request_id, &msg->payload+2*sizeof(int),sizeof(int));
    memcpy(&src, &msg->src,sizeof(int));
    node_to_reply[general_request_id] = {msg->src,msg->id};
    cout << "trail: " << trail << ". dest: " << dest << ". src: " << src << endl;
    char pipe[512*trail];
    read(ret,pipe,sizeof(pipe));
    write(sockets[dest],&pipe,512*trail);
};

////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// GLOBAL METHODS //////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////

string message_type(message* msg) {
    //int func_id = *(int*)(&msg->funcID);;
    int func_id;
    memcpy(&func_id, &msg->funcID, sizeof(int));
    if (func_id==1) return "Ack";
    else if (func_id==2) return "Nack";
    else if (func_id==4) return "Connect";
    else if (func_id==8) return "Discover";
    else if (func_id==16) return "Route";
    else if (func_id==32) return "Send";
    else if (func_id==64) return "Relay";
    return "(Can not identify)";
}

void send_route(message *msg) {
    int general_request_id, destination, length;
    message rply;
    rply.id = random();
    rply.src = id;
    rply.funcID = 16;
    rply.trailMSG = 0;
    if (msg->funcID == 8) { /* need to respond to discover message */
        int path_length = 2;
        memcpy(&destination, msg->payload, sizeof(int));
        memcpy(&general_request_id, msg->payload + sizeof(int), sizeof(int));
        rply.dest = node_to_reply[general_request_id].first;
        memcpy(rply.payload, &general_request_id, sizeof(int));
        memcpy(rply.payload + sizeof(int), &path_length, sizeof(int));
        memcpy(rply.payload + 2*sizeof(int), (&id), sizeof(int));
        memcpy(rply.payload + 3*sizeof(int), &destination, sizeof(int));
        cout << "1) got " << msg->funcID << " msg. sending " << rply.funcID << " msg to " << node_to_reply[general_request_id].first << endl;
        write(sockets[rply.dest], &rply, sizeof(rply));
        return;
    } else if (msg->funcID == 2) { /* need to respond to nack message */
        memcpy(&general_request_id, msg->payload + 3 * sizeof(int), sizeof(int));//get general_request_id
        memcpy(&destination, msg->payload + 4 * sizeof(int), sizeof(int));//get destination
        cout << "2) got " << msg->funcID << " msg. sending " << rply.funcID << " msg to " << node_to_reply[general_request_id].first << endl;
    } else if (msg->funcID == 16) { /* need to respond to route message */
        memcpy(&general_request_id, msg->payload, sizeof(int));
        memcpy(&length, msg->payload + sizeof(int), sizeof(int));
        memcpy(&destination, msg->payload + (2 + length - 1) * sizeof(int),
               sizeof(int));//destination is the last in the path
        cout << "3) got " << msg->funcID << " msg. sending " << rply.funcID << " msg to " << node_to_reply[general_request_id].first << endl;
    }
    length = waze[destination].size() +1;//the path exists beacuse it was added in route function,the +1 is the current node
    memcpy(rply.payload, &general_request_id, sizeof(int));//adding original id to the payload
    memcpy(rply.payload + sizeof(int), &length, sizeof(int));//adding length to the payload
    memcpy(rply.payload + 2 * sizeof(int), &id, sizeof(int));//adding current node to path
    rply.dest = node_to_reply[general_request_id].first;
    auto it = waze[destination].begin();
    int node_id;
    for (int i = 0; i < length - 1; ++i) {
        node_id = *it++;
        memcpy(rply.payload + (i + 3) * sizeof(int), &node_id, sizeof(int));
    }
    //node_to_reply.erase(general_request_id);
    cout << "sending route to " << rply.dest << endl;
    write(sockets[rply.dest], &rply, sizeof(rply));
}

void send_nack(message* msg) {
    int destination, general_request_id;
    message rply; /* nack */
    rply.id=random();
    rply.src=id;
    rply.dest = msg->src;
    rply.trailMSG=0;
    rply.funcID=2;
    if (msg->funcID==2) { /* need to respond to nack message */
        int nack_type;
        memcpy(&nack_type, msg->payload+sizeof(int), sizeof(int));
        /* read and write data to payload in accordance to the nack type */
        if (nack_type==8) { /* the nack message we received responds to discover message */
            memcpy(&general_request_id, msg->payload+2*sizeof(int), sizeof(int));
            memcpy(&destination, msg->payload+3*sizeof(int), sizeof(int));
            int prev_node = node_to_reply[general_request_id].first;
            rply.dest = prev_node;
            /* write to the new message */
            memcpy(rply.payload, &prev_node,sizeof(int));
            memcpy(rply.payload+1*sizeof(int), &nack_type,sizeof(int));
            memcpy(rply.payload+2*sizeof(int), &general_request_id,sizeof(int));
            memcpy(rply.payload+3*sizeof(int), &destination,sizeof(int));
        }
    } else if (msg->funcID==8) { /* need to respond to discover message */
        int nack_type = 8;
        memcpy(&destination, msg->payload, sizeof(int));
        memcpy(&general_request_id, msg->payload+sizeof(int), sizeof(int));
        rply.dest = msg->src;
        /* write to the new message */
        memcpy(rply.payload, &rply.dest,sizeof(int));
        memcpy(rply.payload+1*sizeof(int), &nack_type,sizeof(int));
        memcpy(rply.payload+2*sizeof(int), &general_request_id,sizeof(int));
        memcpy(rply.payload+3*sizeof(int), &destination,sizeof(int));
    } else if (msg->funcID==64) { /* TODO: else if nack_type==64 (relay) */
        memcpy(rply.payload, &msg->id,sizeof(int));
    }
    //node_to_reply.erase(general_request_id);
    cout << "sending nack to " << rply.dest << endl;
    write(sockets[rply.dest],&rply,sizeof(rply));
}

void send_ack(message* msg){
    auto* rply=new message;
    rply->id=random();
    rply->src=id;
    rply->dest=msg->src;
    rply->trailMSG=msg->trailMSG==0?0:msg->trailMSG-1;
    rply->funcID=1;
    memcpy(rply->payload, (char*)&msg->id,sizeof(int));
    write(sockets.at(msg->src),&rply,sizeof(rply));
    delete rply;
}
void send_discover(int dst, int general_request_id) { /* first discover from the terminal */
    message outgoing;
    outgoing.id = rand();

    int first_nei;
    if (node_to_reply[general_request_id].first==-1) { /* src node! */
        node_to_reply[outgoing.id] = {-1, -1}; /* source node! add to the new id an stop condition */
        first_nei = *first_src_neis[dst].begin();
        /* override new discover id */
        memcpy(outgoing.payload+sizeof(int), &outgoing.id, sizeof(int));
    } else { /* inner node */
        first_nei = *nodes_to_request[general_request_id].begin();
        /* add discover id to payload */
        memcpy(outgoing.payload+sizeof(int), &general_request_id, sizeof(int));
    }
    
    outgoing.src = id;
    outgoing.dest = first_nei;
    outgoing.trailMSG = 0;
    outgoing.funcID = 8; /* discover function id is 8 */
    memcpy(outgoing.payload, &dst, sizeof(int)); /* set the payload */
    
    char outgoing_buffer[512];
    memcpy(outgoing_buffer, &outgoing, sizeof(outgoing));
    
    cout << "discovering to " << first_nei << endl;
    send(sockets[first_nei], outgoing_buffer, sizeof(outgoing_buffer), 0);
}

void send_relay(int destination,int original_id) {
    int length=waze[destination].size();
    char pipe[512*(length)];
    message relays;
    relays.funcID=64;
    int msg_id=random();
    node_to_reply[msg_id] = {-1, -1};
    /* starting from 1 to length-1.
       from 1 because the first message is read by the first node to which it is sent
       to length-1 because the last message is "send" message and not "relay" message */
    for (int i = 1; i <= length-1; ++i) {
        relays.id=msg_id+i;
        relays.src=id;
        relays.dest=waze[destination][i];
        relays.trailMSG=length-i;
        int len = length-i;
        memcpy(relays.payload,&waze[destination][i],sizeof(int));
        memcpy(relays.payload+sizeof(int),&len,sizeof(int));
        memcpy(relays.payload+2*sizeof(int),&msg_id,sizeof(int));
        memcpy(pipe+((i-1)*sizeof(relays)),&relays,sizeof(relays));
    }
    message msg;
    msg.src=id;
    msg.dest=destination;
    msg.trailMSG=0;
    msg.funcID=32;
    int txt_length;
    txt_length=strlen(text[destination].c_str());
    string txt_to_send = text[destination];
    memcpy(msg.payload,&txt_length,sizeof(int));
    cout << "sending len: " << txt_length << ". msg: " << txt_to_send << endl;
    memcpy(msg.payload+sizeof(int),txt_to_send.c_str(),txt_length);
    memcpy(pipe+(length-1)*sizeof(message),&msg,sizeof(msg));
    int dest=waze[destination][0];
    text[destination].erase(); /* we wrote the text to the message! remove the text from data structure */
    write(sockets[dest],&pipe,sizeof(pipe));
}