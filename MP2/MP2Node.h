/**********************************
 * FILE NAME: MP2Node.h
 *
 * DESCRIPTION: MP2Node class header file
 **********************************/

#ifndef MP2NODE_H_
#define MP2NODE_H_

/**
 * Header files
 */
#include "stdincludes.h"
#include "EmulNet.h"
#include "Node.h"
#include "HashTable.h"
#include "Log.h"
#include "Params.h"
#include "Message.h"
#include "Queue.h"
#include <sstream>
#include <utility>

/**
 * CLASS NAME: MP2Node
 *
 * DESCRIPTION: This class encapsulates all the key-value store functionality
 * 				including:
 * 				1) Ring
 * 				2) Stabilization Protocol
 * 				3) Server side CRUD APIs
 * 				4) Client side CRUD APIs
 */
class MP2Node {
private:
	// Vector holding the next two neighbors in the ring who have my replicas
	vector<Node> hasMyReplicas;
	// Vector holding the previous two neighbors in the ring whose replicas I have
	vector<Node> haveReplicasOf;
	// Ring
	vector<Node> ring;
	// Hash Table
	HashTable * ht;
	// Member representing this member
	Member *memberNode;
	// Params object
	Params *par;
	// Object of EmulNet
	EmulNet * emulNet;
	// Object of Log
	Log * log;

    //My Address
    Address * myAddr;

    //Maps a transaction onto the number of replies
    map<int, int> replica_create_replies;
    map<int, int> replica_delete_replies;
    map<int, int> replica_read_replies;
    map<int, int> replica_update_replies;
    
    //Maps a trancsaction to a key
    map<int, string> transID_to_key;
    map<int, int> transID_to_timestamp;

public:
	MP2Node(Member *memberNode, Params *par, EmulNet *emulNet, Log *log, Address *addressOfMember);

	Member * getMemberNode() {return this->memberNode;}

	// ring functionalities
	void updateRing();
	vector<Node> getMembershipList();
	size_t hashFunction(string key);
	void findNeighbors();

	// client side CRUD APIs
	void clientCreate(string key, string value);
	void clientRead(string key);
	void clientUpdate(string key, string value);
	void clientDelete(string key);
    void clientCRUDHelper(string key, string value, MessageType type);
    Message consMessage(int transID, MessageType type, string key, string value, ReplicaType replica);

	// receive messages from Emulnet
	bool recvLoop();
	static int enqueueWrapper(void *env, char *buff, int size);

	// handle messages from receiving queue
	void checkMessages();

	// find the addresses of nodes that are responsible for a key
	vector<Node> findNodes(string key);

	// server
	bool createKeyValue(string key, string value, ReplicaType replica, int transID);
	string readKey(string key, int transID);
	bool updateKeyValue(string key, string value, ReplicaType replica, int transID);
	bool deletekey(string key, int transID);
    void replyReceived(bool success, int transID, Address fromAddr);
    void readreplyReceived(string value, int transID, Address fromAddr);

	// stabilization protocol - handle multiple failures
	void stabilizationProtocol();

	// Dispatch message to corresponding address
	void dispatchMessage(Message message, Address * toAddr);

	~MP2Node();    
};

#endif /* MP2NODE_H_ */
