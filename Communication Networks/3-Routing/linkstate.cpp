/*
    Link State Routing Program
    ECE 438 - FA 15
    MP 3
    Sarosh Ahmad
    sahmad13
*/
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <list>
#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <stdio.h>

#define MAX_NODES 99
#define DISCONNECT_DIST 999999
#define UNDEF_VERTEX -1

typedef struct {int source; int dest; std::string message; } message_t;

std::list<int> vertices;
std::list<message_t> messages;
int adjacencyMatrix[MAX_NODES][MAX_NODES];
std::vector<std::map<int, int>> forwardingTable(MAX_NODES);

void shortestPaths( int source )
{
    //Run Dijkstra's 
    std::list<int> unvisited;       //Holds unvisited vertices
    std::map<int, int> distance;    //Maps vertex to distance of that vertex from source
    std::map<int, int> predecessor; //Maps vertex to its predecessor along the shortest path

    //Initialization
    for( auto it=vertices.begin() ; it!=vertices.end() ; ++it )
    {
        distance[*it] = DISCONNECT_DIST;
        predecessor[*it] = UNDEF_VERTEX;
        unvisited.push_back(*it);
    }
    distance[source] = 0;
    //Loop
    while( !unvisited.empty() )
    {
        // Select vertex with least distance from source
        int min_dist = DISCONNECT_DIST;
        int min_dist_vertex;
        //Find vertex in unvisited with least distance from source
        for( auto it=unvisited.begin() ; it!=unvisited.end() ; ++it )
            if( distance[*it] <= min_dist )
            {
                min_dist = distance[*it];
                min_dist_vertex = *it;
            }
        unvisited.remove(min_dist_vertex);

        for( auto it=vertices.begin() ; it!=vertices.end() ; ++it )
        {
            int alt;
            if( adjacencyMatrix[min_dist_vertex][*it] != -999 && 
                                adjacencyMatrix[min_dist_vertex][*it] != 0)
            {
                alt = distance[min_dist_vertex] + adjacencyMatrix[min_dist_vertex][*it];
                if( alt < distance[*it] )
                {
                    distance[*it] = alt;
                    predecessor[*it] = min_dist_vertex;
                }
            }
        }
    }

    //Print Forwarding Table to output.txt and insert entry into forwardingTable[u]
    std::ofstream fout;
    fout.open("output.txt", std::ios::app);
    for( auto it=vertices.begin() ; it!=vertices.end() ; ++it )
    {
        int dest = *it;
        int nexthop = dest;
        int pred = predecessor[dest];
        if( dest == source )
        {
            fout<<*it<<" "<<*it<<" "<<0<<std::endl;
            forwardingTable[source].insert( std::pair<int,int>(source, source) );
        }
        else
        if( distance[*it] == DISCONNECT_DIST )
        {
            fout<<*it<<" "<<-999<<" "<<-999<<std::endl;    
            forwardingTable[source].insert( std::pair<int,int>(*it, -999) );
        }
        else
        {
            while( pred != source )
            {
                nexthop = pred;
                pred = predecessor[pred];

            }   
            fout<<*it<<" "<<nexthop<<" "<<distance[*it]<<std::endl;
            forwardingTable[source].insert( std::pair<int,int>(*it, nexthop) );            
        }
    }
    fout<<std::endl;
    fout.close();

}
    

void runDijkstra()
{
    std::list<int>::iterator it;
    for( it=vertices.begin() ; it!=vertices.end() ; ++it )
        shortestPaths( *it );
}

void sendMessages()
{

    std::ofstream fout;
    fout.open("output.txt", std::ios::app);

    for( auto it = messages.begin() ; it!=messages.end() ; ++it )
    {
        int msg_src, msg_dest;
        std::string msg( (*it).message );
        msg_src = (*it).source;
        msg_dest = (*it).dest;
        int nexthop = msg_src;
        int u = msg_src;

        fout<<msg_src<<" to "<<msg_dest<<" hops ";
        while( nexthop != msg_dest )
        {
            fout<<nexthop<<" ";
            nexthop = forwardingTable[u].at(msg_dest);
            u = nexthop;
        }
        fout<<"message "<<msg<<std::endl;
    }
    fout<<std::endl;
    fout.close();
}

int main( int argc, char *argv[] )
{
    std::string topoFile, messageFile, changesFile;

    for( int i=0; i<MAX_NODES ; i++ )
        for( int j=0 ; j<MAX_NODES ; j++ )
        {
            if( i == j )
                adjacencyMatrix[i][j] = 0;
            else
                adjacencyMatrix[i][j] = -999;
        }

    if( argc != 4 )
    {
        std::cout<<"Usage: ./linkstate topofile messagefile changesfile\n";
        return -1;
    }
    else
    {
        //Parse filenames
        topoFile = argv[1];
        messageFile = argv[2];
        changesFile = argv[3];
    }

    //Build topology graph from topofile
    std::ifstream inTopo(topoFile);
    if( !inTopo )
    {
        std::cout<<"Error opening topofile\n";
        return -1;
    }
    else
    {
        std::string line;
        int node1, node2, cost;
        while( inTopo >> node1 >> node2 >> cost )
        {
            if( !std::binary_search(vertices.begin(), vertices.end(), node1) )
            {
                vertices.push_back( node1 );
                vertices.sort();
            }
            if( !std::binary_search(vertices.begin(), vertices.end(), node2) )
            {
                vertices.push_back( node2 );
                vertices.sort();
            }
            adjacencyMatrix[node1][node2] = cost;
            adjacencyMatrix[node2][node1] = cost;
        }
    }
    inTopo.close();

    //Parse message file
    std::ifstream inMessage(messageFile);
    if( !inMessage )
    {
        std::cout<<"Error opening messagefile\n";
        return -1;
    }
    else
    {
        int src, dest;
        char readMessage[50];
        std::string temp;
        while( std::getline(inMessage, temp) )
        {
            sscanf(temp.c_str(), "%d %d %s", &src, &dest, readMessage);
            message_t msg;
            msg.source = src;
            msg.dest = dest;
            msg.message = std::string(readMessage);
            messages.push_back( msg );
        }
    }
    inMessage.close();

    runDijkstra();
    sendMessages();

    //Read changesFile and for each change made to topology, run Dijkstra and output to file,
    //and send messages
    std::ifstream inChanges(changesFile);
    if( !inChanges )
    {
        std::cout<<"Error opening changesfile\n";
        return -1;
    }
    else
    {
        int node1, node2, cost;
        while( inChanges >> node1 >> node2 >> cost )
        {
            adjacencyMatrix[node1][node2] = cost;
            adjacencyMatrix[node2][node1] = cost;
            runDijkstra();
            sendMessages();
        }
    }
    inChanges.close();

    return 0;
}