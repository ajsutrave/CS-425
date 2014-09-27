/**********************
*
* Progam Name: MP1. Membership Protocol
* 
* Code authors: AJ Sutrave
*
* Current file: mp1_node.c
* About this file: Member Node Implementation
* 
***********************/

#include "mp1_node.h"
#include "emulnet.h"
#include "MPtemplate.h"
#include "log.h"

/*
 *
 * Routines for introducer and current time.
 *
 */

char NULLADDR_CHAR[] = {0,0,0,0,0,0};
address* NULLADDR = (address *) NULLADDR_CHAR;
int isnulladdr( address *addr ){
    return (memcmp(addr, NULLADDR, sizeof(address))==0?1:0);
}

//Compare addresses
int addrcmp(address addr1, address addr2){
    return (memcmp(&addr1, &addr2, 6)==0?1:0);
}

void addr_to_str(address addr, char * str) {
	sprintf(str, "%d.%d.%d.%d:%d ", addr.addr[0], addr.addr[1], addr.addr[2], addr.addr[3], *(short *)&addr.addr[4]);
}

void print_memberlist(memlist_entry * ptr) {
    char s [30];
    int member_cnt = 0;
    while(ptr != NULL){
        member_cnt++;
        addr_to_str(ptr->addr, s);
        printf("Member %d: %s t(%d) hb(%ld)\n", member_cnt, s, ptr->time, ptr->heartbeat);
        ptr = ptr->next;
    }
}

void add_entry_to_memberlist(member * thisNode, memlist_entry new_member) {
    memlist_entry * new_entry = malloc(sizeof(memlist_entry));
    memcpy(new_entry, &new_member, sizeof(memlist_entry));
    new_entry->next = thisNode->memberlist;
    thisNode->memberlist = new_entry;
}

/* 
Return the address of the introducer member. 
*/
address getjoinaddr(void){

    address joinaddr;

    memset(&joinaddr, 0, sizeof(address));
    *(int *)(&joinaddr.addr)=1;
    *(short *)(&joinaddr.addr[4])=0;
    
    return joinaddr;
}

/*
 *
 * Message Processing routines.
 *
 */

/* 
Received a JOINREQ (joinrequest) message.
*/
void Process_joinreq(void *env, char *data, int size)
{
    member *thisNode = (member*) env;
    address *addedAddr = (address*) data;
    int num_members = 0; 
    messagehdr *msg;
    memlist_entry *newMember, *newMember_prev;
    char s1[30];//, s2[30];
    addr_to_str(thisNode->addr, s1);

    //Create a new member list entry
    newMember = malloc(sizeof(memlist_entry));
    newMember->next = NULL;
    memcpy(&newMember->addr, addedAddr, sizeof(address));

    //Check if this is the first entry
    newMember_prev = thisNode->memberlist;
    if (newMember_prev == NULL) {
        //If so it is the head of the LL
        thisNode->memberlist = newMember;
    }
    else { //Find the end of the memberlist
        while(newMember_prev->next!=NULL) {
            newMember_prev = newMember_prev->next;
            num_members++;
        }
        //Add the new member to the membership list
        newMember_prev->next = newMember;
    }

    //Update the log and member info
    //printf("\n%s has %d members \n", s1, thisNode->num_members);
    thisNode->num_members++;
#ifdef DEBUGLOG
    logNodeAdd(&thisNode->addr, addedAddr);
#endif

    //Construct the JOINREP msg
    size_t msgsize = sizeof(messagehdr) 
        + sizeof(memlist_entry)*(num_members + 1); //Include self in member list
    //printf("\nmsgsize is %d * %d = %d\n", sizeof(member), num_members, msgsize);    
    msg=malloc(msgsize);
    msg->msgtype=JOINREP;

    //Fill it with members
    int i = 0;
    memlist_entry * curr = thisNode->memberlist;
    char * dest_ptr = (char *)(msg+1);
    for(i=0; i< num_members; i++){ //Dont send the node to itself
        memcpy(dest_ptr, curr, sizeof(memlist_entry));
        curr = curr->next;
        dest_ptr += sizeof(memlist_entry);
    }    
    
    //Include self in JOINREP
    memlist_entry self;
    self.addr = thisNode->addr;
    self.time = getcurrtime();
    self.heartbeat = thisNode->heartbeat;
    memcpy(dest_ptr, &self, sizeof(memlist_entry));

    //Send the JOINREP
    MPp2psend(&thisNode->addr, addedAddr, (char *)msg, msgsize);
    free(msg);
    thisNode->heartbeat++;
}

void serialize_memberlist(memlist_entry * memlist_arr, member * node) {
    int i = 0;
    memlist_entry * curr = node->memberlist;
    char addr_str [30]; addr_to_str(node->addr, addr_str);
    printf("Serialized Memberlist for %s: ", addr_str);
    for(i=0; i < node->num_members; i++){
        memcpy(&memlist_arr[i], curr, sizeof(memlist_entry));
        char member_str [30]; addr_to_str(curr->addr, member_str);
        printf("%s ", member_str);
        curr = curr->next;
    }    
    printf("\n");
}

/* 
Received a JOINREP (joinreply) message. 
*/
void Process_joinrep(void *env, char *data, int size)
{
    member *thisNode = (member*) env;
    memlist_entry* recvd_memberlist = (memlist_entry * ) data;
    int num_members = size/(sizeof(memlist_entry));
    int i;

    memlist_entry * curr;
    memlist_entry * prev = NULL;
    for (i = 0; i < num_members; i++) {

        //Create a copy of the entry
        curr = malloc(sizeof(memlist_entry));
        memcpy(curr, &recvd_memberlist[i], sizeof(memlist_entry));
        
        //Link list
        if (prev != NULL) { 
            prev->next = curr;
        }
        //Handle edge case of head
        else {
            thisNode->memberlist = curr;
        }
        prev = curr;
#ifdef DEBUGLOG
        logNodeAdd(&thisNode->addr, &curr->addr);
#endif
    }
    curr->next = NULL; //Terminate the memberlist
    
    //Update the node
    thisNode->ingroup = 1;
    thisNode->num_members = num_members;
    
    /* char s [30]; addr_to_str(thisNode->addr, s);  */
    /* printf("\n%s has %d members \n", s, thisNode->num_members); */
    /* print_memberlist(thisNode->memberlist); */
}

void Process_gossip(void *env, char *data, int size){
    member *thisNode = (member*) env;
    memlist_entry * recvd_memberlist = (memlist_entry*) data;
    int recvd_num_members = (size - sizeof(address))/sizeof(memlist_entry);
    char recvr_str [30] = {0}; addr_to_str(thisNode->addr, recvr_str); 
    int i;//, j;
    
    /* printf("%s : GOSSIP about %d members\n", recvr_str, recvd_num_members); */
    for (i = 0; i < recvd_num_members; i++) {
        int in_membership = 0;
        memlist_entry * curr = thisNode->memberlist;

        //Ignore gossip about self
        if ( addrcmp(thisNode->addr, recvd_memberlist[i].addr) )
            continue;

        //Check for through the member list
        while(curr!=NULL) {
            /* char member_str [30]; addr_to_str(recvd_memberlist[i].addr, member_str); */
            /* printf("%s == %s? %d\n",  */
            /*        recvr_str,  */
            /*        member_str,  */
            /*        addrcmp(thisNode->addr, recvd_memberlist[i].addr)); */

             if ( addrcmp(curr->addr,recvd_memberlist[i].addr) ) {
                in_membership = 1;
                break;
            }
            curr = curr->next;
        }
        
        
                    
        if(in_membership) {
            //Update member information
            
        }
        else {
            char member_str [30]; addr_to_str(recvd_memberlist[i].addr, member_str);
            printf("%s : Received GOSSIP about new member %s\n", recvr_str, member_str);
#ifdef DEBUGLOG
            logNodeAdd(&thisNode->addr, &recvd_memberlist[i].addr);
#endif
            add_entry_to_memberlist(thisNode, recvd_memberlist[i]);
            thisNode->num_members++;
        }
    }
    /* printf("\n"); */
}


/* 
Array of Message handlers. 
*/
void ( ( * MsgHandler [20] ) STDCLLBKARGS )={
/* Message processing operations at the P2P layer. */
    Process_joinreq, 
    Process_joinrep,
    Process_gossip,
};

/* 
Called from nodeloop() on each received packet dequeue()-ed from node->inmsgq. 
Parse the packet, extract information and process. 
env is member *node, data is 'messagehdr'. 
*/
int recv_callback(void *env, char *data, int size){

    member *thisNode = (member *) env;
    messagehdr *msghdr = (messagehdr *)data;
    //address sender = msghdr->sender;
    char *pktdata = (char *)(msghdr+1);

    if(size < sizeof(messagehdr)){
#ifdef DEBUGLOG
        LOG(&((member *)env)->addr, "Faulty packet received - ignoring");
#endif
        return -1;
    }

#ifdef DEBUGLOG
    LOG(&((member *)env)->addr, "Received msg type %d with %d B payload", msghdr->msgtype, size - sizeof(messagehdr));
#endif

    if((thisNode->ingroup && msghdr->msgtype >= 0 && msghdr->msgtype <= DUMMYLASTMSGTYPE)
        || (!thisNode->ingroup && msghdr->msgtype==JOINREP))            
            /* if not yet in group, accept only JOINREPs */
        MsgHandler[msghdr->msgtype](env, pktdata, size-sizeof(messagehdr));
    /* else ignore (garbled message) */
    free(data);
    thisNode->heartbeat++;

    return 0;

}

/*
 *
 * Initialization and cleanup routines.
 *
 */

/* 
Find out who I am, and start up. 
*/
int init_thisNode(member *thisNode, address *joinaddr){
    
    if(MPinit(&thisNode->addr, PORTNUM, (char *)joinaddr)== NULL){ /* Calls ENInit */
#ifdef DEBUGLOG
        LOG(&thisNode->addr, "MPInit failed");
#endif
        exit(1);
    }
#ifdef DEBUGLOG
    else LOG(&thisNode->addr, "MPInit succeeded. Hello.");
#endif

    thisNode->bfailed=0;
    thisNode->inited=1;
    thisNode->ingroup=0;
    thisNode->time=getcurrtime();
    thisNode->heartbeat=0;
    thisNode->num_members = 0;

    thisNode->memberlist = NULL;

    /* node is up! */
    return 0;
}


/* 
Clean up this node. 
*/
int finishup_thisnode(member *node){

	/* <your code goes in here> */
    //TODO clear memory for node
    return 0;
}


/* 
 *
 * Main code for a node 
 *
 */

/* 
Introduce self to group. 
*/
int introduceselftogroup(member *node, address *joinaddr){
    
    messagehdr *msg;
#ifdef DEBUGLOG
    static char s[1024];
#endif

    if(memcmp(&node->addr, joinaddr, 4*sizeof(char)) == 0){
        /* I am the group booter (first process to join the group). Boot up the group. */
#ifdef DEBUGLOG
        LOG(&node->addr, "Starting up group...");
#endif

        node->ingroup = 1;
    }
    else{
        size_t msgsize = sizeof(messagehdr) + sizeof(address);
        msg=malloc(msgsize);

    /* create JOINREQ message: format of data is {struct address myaddr} */
        msg->msgtype=JOINREQ;
        memcpy((char *)(msg+1), &node->addr, sizeof(address));

#ifdef DEBUGLOG
        sprintf(s, "Trying to join...");
        LOG(&node->addr, s);
#endif

    /* send JOINREQ message to introducer member. */
        MPp2psend(&node->addr, joinaddr, (char *)msg, msgsize);
        
        free(msg);
    }

    return 1;

}

/* 
Called from nodeloop(). 
*/
void checkmsgs(member *node){
    void *data;
    int size;

    /* Dequeue waiting messages from node->inmsgq and process them. */
	
    while((data = dequeue(&node->inmsgq, &size)) != NULL) {
        recv_callback((void *)node, data, size); 
    }
    return;
}


/* 
Executed periodically for each member. 
Performs necessary periodic operations. 
Called by nodeloop(). 
*/
void nodeloopops(member *thisNode) {
    int random_num = rand() % thisNode->num_members;
    char sender_addr [30]; addr_to_str(thisNode->addr, sender_addr); 
    int i;
    memlist_entry * recvr_node = thisNode->memberlist;
    messagehdr *msg;

    char s [30]; addr_to_str(thisNode->addr, s);
    printf("\n%s has %d members \n", s, thisNode->num_members);
    print_memberlist(thisNode->memberlist);
    
    //Pick a random node to ping
    for (i=0; i<random_num; i++)
        recvr_node  = recvr_node->next;
    char recvr_addr [30]; addr_to_str(recvr_node->addr, recvr_addr); 
    
    //create GOSSIP message
    size_t msgsize = sizeof(messagehdr) 
        + sizeof(memlist_entry)*(thisNode->num_members + 1); //Include self in member list
    msg=malloc(msgsize);
    msg->msgtype=GOSSIP;
    msg->sender = thisNode->addr;

    //Fill the message with the current node's memberlist 
    memlist_entry * msgbody = (memlist_entry*) (msg+1);
    serialize_memberlist(msgbody, thisNode);

    //Include self at end of list
    memlist_entry * self = msgbody + thisNode->num_members;
    self->addr = thisNode->addr;
    self->time = getcurrtime();
    self->heartbeat = thisNode->heartbeat;

    /* printf("Sending GOSSIP msg with following members : "); */
    /* for (i=0; i < thisNode->num_members; i++){ */
    /*     char member_addr [30]; addr_to_str(msgbody[i].addr, member_addr); */
    /*     printf("%s ", member_addr); */
    /* } */
    /* printf("\n"); */

    //send GOSSIP message 
    MPp2psend(&thisNode->addr, &recvr_node->addr, (char *)msg, msgsize);
    free(msg);

    thisNode->heartbeat++;
}

/* 
Executed periodically at each member. Called from app.c.
*/
void nodeloop(member *node){
    if (node->bfailed) return;

    checkmsgs(node);

    /* Wait until you're in the group... */
    if(!node->ingroup) return ;

    /* ...then jump in and share your responsibilites! */
    nodeloopops(node);
    
    return;
}

/* 
All initialization routines for a member. Called by app.c. 
*/
void nodestart(member *node, char *servaddrstr, short servport){

    address joinaddr=getjoinaddr();

    /* Self booting routines */
    if(init_thisNode(node, &joinaddr) == -1){

#ifdef DEBUGLOG
        LOG(&node->addr, "init_thisNode failed. Exit.");
#endif
        exit(1);
    }

    char my_addr [30]; addr_to_str(node->addr, my_addr); 
    printf(" %s\n", my_addr); //print new nodes addr

    if(!introduceselftogroup(node, &joinaddr)){
        finishup_thisnode(node);
#ifdef DEBUGLOG
        LOG(&node->addr, "Unable to join self to group. Exiting.");
#endif
        exit(1);
    }

    return;
}

/* 
   Enqueue a message (buff) onto the queue env. 
*/
int enqueue_wrppr(void *env, char *buff, int size) {return enqueue((queue *)env, buff, size);}

/* 
Called by a member to receive messages currently waiting for it. 
*/
int recvloop(member *node){
    if (node->bfailed) return -1;
    else return MPrecv(&(node->addr), enqueue_wrppr, NULL, 1, &node->inmsgq); 
    /* Fourth parameter specifies number of times to 'loop'. */
}

