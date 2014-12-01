/**********************************
 * FILE NAME: MP2Node.cpp
 *
 * DESCRIPTION: MP2Node class definition
 **********************************/
#include "MP2Node.h"

static const string NO_VALUE;
static const Message INVALID_MESSAGE(0, Address(), CREATE, "", "");
static const int QUORUM_SUCCESS = 2;
static const int QUORUM_FAIL = -2;

static int my_g_transID = 0;

/**
 * constructor
 */
MP2Node::MP2Node(Member *memberNode, Params *par, EmulNet * emulNet, Log * log, Address * address) {
	this->memberNode = memberNode;
	this->par = par;
	this->emulNet = emulNet;
	this->log = log;
	ht = new HashTable();
	this->memberNode->addr = *address;
    myAddr = &this->memberNode->addr;
    
}

/**
 * Destructor
 */
MP2Node::~MP2Node() {
	delete ht;
	delete memberNode;
}

/**
 * FUNCTION NAME: updateRing
 *
 * DESCRIPTION: This function does the following:
 * 				1) Gets the current membership list from the Membership Protocol (MP1Node)
 * 				   The membership list is returned as a vector of Nodes. See Node class in Node.h
 * 				2) Constructs the ring based on the membership list
 * 				3) Calls the Stabilization Protocol
 */
void MP2Node::updateRing() {
	vector<Node> curMemList;
	bool change = false;

	/*
	 *  Step 1. Get the current membership list from Membership Protocol / MP1
	 */
	curMemList = getMembershipList();

	/*
	 * Step 2: Construct the ring
	 */
	// Sort the list based on the hashCode
	sort(curMemList.begin(), curMemList.end());
    
    //Compare the received membership list with the ring
    if (curMemList.size()!=ring.size()) {
        change = true;
        ring = curMemList;
    }
    else {
        vector<Node>::iterator CML_it, ring_end, ring_it;
        for(CML_it=curMemList.begin(), ring_it=ring.begin(), ring_end=ring.end(); 
            ring_it != ring_end; ++ring_it, ++CML_it) {

            if( (*CML_it).getHashCode() != (*ring_it).getHashCode() ) {
                change = true;
                break;
            }
        }
    }        

	/*
	 * Step 3: Run the stabilization protocol IF REQUIRED
	 */
	// Run stabilization protocol if the hash table size is greater than zero and if there has been a changed in the ring
    if (!ht->isEmpty() && change)
        stabilizationProtocol();

    if (change) {
        stringstream ring_ss; 
        ring_ss << "Ring :";
        for(unsigned i = 0; i < ring.size(); i++)
            ring_ss << ring[i].nodeAddress.getAddress() << ", ";
        log->LOG(myAddr, ring_ss.str().c_str());
    }
}

/**
 * FUNCTION NAME: getMemberhipList
 *
 * DESCRIPTION: This function goes through the membership list from the Membership protocol/MP1 and
 * 				i) generates the hash code for each member
 * 				ii) populates the ring member in MP2Node class
 * 				It returns a vector of Nodes. Each element in the vector contain the following fields:
 * 				a) Address of the node
 * 				b) Hash code obtained by consistent hashing of the Address
 */
vector<Node> MP2Node::getMembershipList() {
	unsigned int i;
	vector<Node> curMemList;
	for ( i = 0 ; i < this->memberNode->memberList.size(); i++ ) {
		Address addressOfThisMember;
		int id = this->memberNode->memberList.at(i).getid();
		short port = this->memberNode->memberList.at(i).getport();
		memcpy(&addressOfThisMember.addr[0], &id, sizeof(int));
		memcpy(&addressOfThisMember.addr[4], &port, sizeof(short));
		curMemList.emplace_back(Node(addressOfThisMember));
	}
	return curMemList;
}

/**
 * FUNCTION NAME: hashFunction
 *
 * DESCRIPTION: This functions hashes the key and returns the position on the ring
 * 				HASH FUNCTION USED FOR CONSISTENT HASHING
 *
 * RETURNS:
 * size_t position on the ring
 */
size_t MP2Node::hashFunction(string key) {
	std::hash<string> hashFunc;
	size_t ret = hashFunc(key);
	return ret%RING_SIZE;
}

/**
 * FUNCTION NAME: clientCreate
 *
 * DESCRIPTION: client side CREATE API
 * 				The function does the following:
 * 				1) Constructs the message
 * 				2) Finds the replicas of this key
 * 				3) Sends a message to the replica
 */
void MP2Node::clientCreate(string key, string value) {
     clientCRUDHelper(key, value, CREATE);
}

/**
 * FUNCTION NAME: clientRead
 *
 * DESCRIPTION: client side READ API
 * 				The function does the following:
 * 				1) Constructs the message
 * 				2) Finds the replicas of this key
 * 				3) Sends a message to the replica
 */
void MP2Node::clientRead(string key){
     clientCRUDHelper(key, NO_VALUE, READ);
}

/**
 * FUNCTION NAME: clientUpdate
 *
 * DESCRIPTION: client side UPDATE API
 * 				The function does the following:
 * 				1) Constructs the message
 * 				2) Finds the replicas of this key
 * 				3) Sends a message to the replica
 */
void MP2Node::clientUpdate(string key, string value){
     clientCRUDHelper(key, value, UPDATE);
}

/**
 * FUNCTION NAME: clientDelete
 *
 * DESCRIPTION: client side DELETE API
 * 				The function does the following:
 * 				1) Constructs the message
 * 				2) Finds the replicas of this key
 * 				3) Sends a message to the replica
 */
void MP2Node::clientDelete(string key){
     clientCRUDHelper(key, NO_VALUE, DELETE);
}

/**
 * FUNCTION NAME: createKeyValue
 *
 * DESCRIPTION: Server side CREATE API
 * 			   	The function does the following:
 * 			   	1) Inserts key value into the local hash table
 * 			   	2) Return true or false based on success or failure
 */
bool MP2Node::createKeyValue(string key, string value, ReplicaType replica, int transID) {

	// Insert key, value, replicaType into the hash table
    string ht_entry = Entry(value, my_g_transID, replica).convertToString();
    bool success = ht->create(key, ht_entry);

    if (success) {log->logCreateSuccess(myAddr, false, transID, key, ht_entry);}
    else {log->logCreateFail(myAddr, false, transID, key, ht_entry);}

    return success;
}

/**
 * FUNCTION NAME: readKey
 *
 * DESCRIPTION: Server side READ API
 * 			    This function does the following:
 * 			    1) Read key from local hash table
 * 			    2) Return value
 */
string MP2Node::readKey(string key, int transID) {
    string retval;
    if (ht->count(key)==1) {
        retval = ht->read(key);
        log->logReadSuccess(myAddr, false, transID, key, retval);
    }
    else {
        retval = "KEY_NOT_FOUND";
        log->logReadFail(myAddr, false, transID, key);
    }
    
    return retval;
}

/**
 * FUNCTION NAME: updateKeyValue
 *
 * DESCRIPTION: Server side UPDATE API
 * 				This function does the following:
 * 				1) Update the key to the new value in the local hash table
 * 				2) Return true or false based on success or failure
 */
bool MP2Node::updateKeyValue(string key, string value, ReplicaType replica) {
	/*
	 * TODO
	 */
	// Update key in local hash table and return true or false
    return false;
}

/**
 * FUNCTION NAME: deleteKey
 *
 * DESCRIPTION: Server side DELETE API
 * 				This function does the following:
 * 				1) Delete the key from the local hash table
 * 				2) Return true or false based on success or failure
 */
bool MP2Node::deletekey(string key, int transID) {
	// Delete the key from the local hash table
    bool success = ht->deleteKey(key);

    if (success) {log->logDeleteSuccess(myAddr, false, transID, key);}
    else {log->logDeleteFail(myAddr, false, transID, key);}

    return success;
}

/**
 * FUNCTION NAME: checkMessages
 *
 * DESCRIPTION: This function is the message handler of this node.
 * 				This function does the following:
 * 				1) Pops messages from the queue
 * 				2) Handles the messages according to message types
 */
void MP2Node::checkMessages() {
	char * data;
	int size;
    bool success = false;
    string read_str;

    auto e = transID_to_timestamp.end();
    for (auto it = transID_to_timestamp.begin(); it != e; ++it) {
        //cout <<  << " " <<  << endl; 
        int transID = it->first;
        int time_elapsed = par->getcurrtime() - it->second[0];
        if (time_elapsed > TIMEOUT) {
            if (replica_create_replies.count(transID)==1) {
                log->logCreateFail(myAddr, true, transID, key, "TIMEOUT");
            }
                
                
    }


	// dequeue all messages and handle them
	while ( !memberNode->mp2q.empty() ) {

        //Pop a message from the queue
		data = (char *)memberNode->mp2q.front().elt;
		size = memberNode->mp2q.front().size;
		memberNode->mp2q.pop();

		string msg_str(data, data + size);
        Message msg(msg_str);
        //Handle the message types here
        switch(msg.type) {
        case CREATE: 
            success = createKeyValue(msg.key, msg.value, msg.replica, msg.transID); 
            break;

        case READ: 
            read_str = readKey(msg.key, msg.transID); 
            break;

        case UPDATE: //TODO
            success = updateKeyValue(msg.key, msg.value, msg.replica); 
            break;

        case DELETE: 
            success = deletekey(msg.key, msg.transID); 
            break;

        case REPLY: 
            replyReceived(msg.success, msg.transID, msg.fromAddr);            
            break;

        case READREPLY: 
            readreplyReceived(msg.value, msg.transID, msg.fromAddr);
            break;
        }

        if (msg.type==CREATE || msg.type==DELETE || msg.type==UPDATE) {
            Message reply(msg.transID, *myAddr, REPLY, success);
            dispatchMessage(reply, &msg.fromAddr);
        }
        else if (msg.type==READ) {
            Message readreply(msg.transID, *myAddr, read_str);
            dispatchMessage(readreply, &msg.fromAddr);
        }

	}

}

/**
 * FUNCTION NAME: findNodes
 *
 * DESCRIPTION: Find the replicas of the given keyfunction
 * 				This function is responsible for finding the replicas of a key
 */
vector<Node> MP2Node::findNodes(string key) {
	size_t pos = hashFunction(key);
	vector<Node> addr_vec;
	if (ring.size() >= 3) {
		// if pos <= min || pos > max, the leader is the min
		if (pos <= ring.at(0).getHashCode() || pos > ring.at(ring.size()-1).getHashCode()) {
			addr_vec.emplace_back(ring.at(0));
			addr_vec.emplace_back(ring.at(1));
			addr_vec.emplace_back(ring.at(2));
		}
		else {
			// go through the ring until pos <= node
			for (unsigned int i=1; i<ring.size(); i++){
				Node addr = ring.at(i);
				if (pos <= addr.getHashCode()) {
					addr_vec.emplace_back(addr);
					addr_vec.emplace_back(ring.at((i+1)%ring.size()));
					addr_vec.emplace_back(ring.at((i+2)%ring.size()));
					break;
				}
			}
		}
	}
	return addr_vec;
}

/**
 * FUNCTION NAME: recvLoop
 *
 * DESCRIPTION: Receive messages from EmulNet and push into the queue (mp2q)
 */
bool MP2Node::recvLoop() {
    if ( memberNode->bFailed ) {
    	return false;
    }
    else {
    	return emulNet->ENrecv(&(memberNode->addr), this->enqueueWrapper, NULL, 1, &(memberNode->mp2q));
    }
}

/**
 * FUNCTION NAME: enqueueWrapper
 *
 * DESCRIPTION: Enqueue the message from Emulnet into the queue of MP2Node
 */
int MP2Node::enqueueWrapper(void *env, char *buff, int size) {
	Queue q;
	return q.enqueue((queue<q_elt> *)env, (void *)buff, size);
}
/**
 * FUNCTION NAME: stabilizationProtocol
 *
 * DESCRIPTION: This runs the stabilization protocol in case of Node joins and leaves
 * 				It ensures that there always 3 copies of all keys in the DHT at all times
 * 				The function does the following:
o *				1) Ensures that there are three "CORRECT" replicas of all the keys in spite of failures and joins
 *				Note:- "CORRECT" replicas implies that every key is replicated in its two neighboring nodes in the ring
 */
void MP2Node::stabilizationProtocol() {
    //TODO
}

void MP2Node::dispatchMessage(Message message, Address * toAddr) {
    emulNet->ENsend(myAddr, toAddr, message.toString());
}


// The [read]replyReceived function ensures all READ and UPDATE operation
// get QUORUM replies
void MP2Node::replyReceived(bool success, int transID, Address fromAddr) {
    assert(transID_to_key.count(transID)==1 && "No key for this transaction!");
    string key = transID_to_key[transID];

    // Only increment if the transaction exists. It may have already reached 
    // QUORUM and been removed
    if (replica_create_replies.count(transID)==1) {
        if (success)
            replica_create_replies[transID] += 1;
        else
            replica_create_replies[transID] -= 1;
        
        if (replica_create_replies[transID] == QUORUM_SUCCESS) {
            log->logCreateSuccess(myAddr, true, transID, key, "COORD_VAL");
            replica_create_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }

        else if (replica_create_replies[transID] == QUORUM_FAIL) {
            log->logCreateFail(myAddr, true, transID, key, "COORD_VAL");
            replica_create_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }
    }

    else if (replica_delete_replies.count(transID)==1) {
        if (success)
            replica_delete_replies[transID] += 1;
        else
            replica_delete_replies[transID] -= 1;
        
        if (replica_delete_replies[transID] == QUORUM_SUCCESS) {
            log->logDeleteSuccess(myAddr, true, transID, key);
            replica_delete_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }

        else if (replica_delete_replies[transID] == QUORUM_FAIL) {
            log->logDeleteFail(myAddr, true, transID, key);
            replica_delete_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }
    }

}

void MP2Node::readreplyReceived(string value, int transID, Address fromAddr) {
    assert(transID_to_key.count(transID)==1 && "No key for this transaction!");
    string key = transID_to_key[transID];
    
    if (replica_read_replies.count(transID)==1) {

        if (value == "KEY_NOT_FOUND")
            replica_read_replies[transID] -= 1;
        else
            replica_read_replies[transID] += 1;
        
        if (replica_read_replies[transID] == QUORUM_SUCCESS) {
            log->logReadSuccess(myAddr, true, transID, key, value);
            replica_read_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }

        else if (replica_read_replies[transID] == QUORUM_FAIL) {
            log->logReadFail(myAddr, true, transID, key);
            replica_read_replies.erase(transID);
            transID_to_timestamp.erase(transID);
        }
    }
    
}


void MP2Node::clientCRUDHelper(string key, string value, MessageType type) {
    //New transaction
    my_g_transID++;

    //Find the replica nodes
    vector<Node> replicaNodes = findNodes(key);

    //Send the create messages to the replicas
    Message create_msg1 = consMessage(my_g_transID, type, key, value, PRIMARY);
    dispatchMessage(create_msg1, replicaNodes[0].getAddress());

    Message create_msg2 = consMessage(my_g_transID, type, key, value, SECONDARY);
    dispatchMessage(create_msg2, replicaNodes[1].getAddress());

    Message create_msg3 = consMessage(my_g_transID, type, key, value, TERTIARY);     
    dispatchMessage(create_msg3, replicaNodes[2].getAddress());   

    //Sanity check
    assert(replica_create_replies.count(my_g_transID)==0 && "Key is being created!");
    assert(replica_delete_replies.count(my_g_transID)==0 && "Key is being deleted!");
    assert(replica_read_replies.count(my_g_transID)==0 && "Key is being read!");
    assert(transID_to_key.count(my_g_transID)==0 && "transID associated with key!");
    assert(transID_to_timestamp.count(my_g_transID)==0 && "transID associated with timestamp!");

    //Make this key pending till REPLY'd
    transID_to_key[my_g_transID] = key;
    transID_to_timestamp[my_g_transID] = par->getcurrtime();
    switch(type) {
    case CREATE: replica_create_replies[my_g_transID] = 0; break;
    case DELETE: replica_delete_replies[my_g_transID] = 0; break;
    case READ: replica_read_replies[my_g_transID] = 0; break;
    case UPDATE: 
    default: assert(false && "TODO Not implemented...");
    }
}

Message MP2Node::consMessage(int transID, MessageType type, string key, string value, ReplicaType replica) {
    //TODO this is really a job for the message class...
    Message retval = INVALID_MESSAGE;
    
    switch(type) {
    case CREATE:
    case UPDATE: //Create or update
        retval = Message(transID, *myAddr, type, key, value, replica);
        break;
    case DELETE: 
    case READ: //Read or delete
        assert(value==NO_VALUE);
        retval = Message(transID, *myAddr, type, key);
        break;
    default:
        assert(false && "Unsupported type");
    }
    return retval;
}
