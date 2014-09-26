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
int addrcmp(address *addr1, address *addr2){
    return (memcmp(addr1, addr2, 6)==0?1:0);
}

void addr_to_str(address * addr, char * str) {
	sprintf(str, "%d.%d.%d.%d:%d ", addr->addr[0], addr->addr[1], addr->addr[2], addr->addr[3], *(short *)&addr->addr[4]);
}


void print_memberlist(memlist_entry * ptr) {
    char s [30];
    int member_cnt = 0;
    while(ptr != NULL){
        member_cnt++;
        addr_to_str(&ptr->addr, s);
        printf("Member %d: %s\n", member_cnt, s);
    }
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
    member *thisnode = (member*) env;
    address *addedAddr = (address*) data;
    int num_members = 0; 
    messagehdr *msg;
    memlist_entry *newMember, *newMember_prev;
    char s1[30];//, s2[30];
    addr_to_str(&thisnode->addr, s1);

    printf("\n*****\n");

    newMember = malloc(sizeof(memlist_entry));
    newMember->next = NULL;
    memcpy(&newMember->addr, addedAddr, sizeof(address));

    newMember_prev = thisnode->memberlist;
    if (newMember_prev == NULL) {
        thisnode->memberlist = newMember;
    }
    else { //Find the end of the memberlist
        while(newMember_prev->next!=NULL) {
            newMember_prev = newMember_prev->next;
            num_members++;
        }
        //Add the new member to the membership list
        newMember_prev->next = newMember;
    }

    printf("\n%s has %d members \n", s1, num_members);


#ifdef DEBUGLOG
    logNodeAdd(&thisnode->addr, addedAddr);
#endif

    //Construct the JOINREP msg
    size_t msgsize = sizeof(messagehdr) 
        + sizeof(memlist_entry)*(num_members + 1); //Include self in member list
    //printf("\nmsgsize is %d * %d = %d\n", sizeof(member), num_members, msgsize);    
    msg=malloc(msgsize);
    msg->msgtype=JOINREP;

    //Fill it with members
    int i = 0;
    memlist_entry * curr = thisnode->memberlist;
    char * dest_ptr = (char *)(msg+1);
    for(i=0; i< num_members; i++){ //Dont send the node to itself
        memcpy(dest_ptr, curr, sizeof(memlist_entry));
        curr = curr->next;
        dest_ptr += sizeof(memlist_entry);
    }    
    
    //Include self in member list
    memlist_entry self;
    self.addr = thisnode->addr;
    self.time = thisnode->time;
    self.hb = thisnode->hb;
    memcpy(dest_ptr, &self, sizeof(memlist_entry));

    //Send the JOINREP
    MPp2psend(&thisnode->addr, addedAddr, (char *)msg, msgsize);
    free(msg);

    return;
}

/* 
Received a JOINREP (joinreply) message. 
*/
void Process_joinrep(void *env, char *data, int size)
{
    member *thisnode = (member*) env;
    memlist_entry* recvd_memberlist = (memlist_entry * ) data;
    int num_members = size/(sizeof(memlist_entry));
    int i;

    memlist_entry * curr;
    memlist_entry * prev = NULL;
    for (i = 0; i < num_members; i++) {
        curr = malloc(sizeof(memlist_entry));
        memcpy(curr, &recvd_memberlist[i], sizeof(memlist_entry));
        if (prev != NULL) prev->next = curr;
        prev = curr;
#ifdef DEBUGLOG
        logNodeAdd(&thisnode->addr, &curr->addr);
#endif
    }
    return;
}

void Process_ping(void *env, char *data, int size){
    //TODO
}
void Process_ack(void *env, char *data, int size){
    //TODO
}

/* 
Array of Message handlers. 
*/
void ( ( * MsgHandler [20] ) STDCLLBKARGS )={
/* Message processing operations at the P2P layer. */
    Process_joinreq, 
    Process_joinrep,
    Process_ping,
    Process_ack
};

/* 
Called from nodeloop() on each received packet dequeue()-ed from node->inmsgq. 
Parse the packet, extract information and process. 
env is member *node, data is 'messagehdr'. 
*/
int recv_callback(void *env, char *data, int size){

    member *node = (member *) env;
    messagehdr *msghdr = (messagehdr *)data;
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

    if((node->ingroup && msghdr->msgtype >= 0 && msghdr->msgtype <= DUMMYLASTMSGTYPE)
        || (!node->ingroup && msghdr->msgtype==JOINREP))            
            /* if not yet in group, accept only JOINREPs */
        MsgHandler[msghdr->msgtype](env, pktdata, size-sizeof(messagehdr));
    /* else ignore (garbled message) */
    free(data);

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
int init_thisnode(member *thisnode, address *joinaddr){
    
    if(MPinit(&thisnode->addr, PORTNUM, (char *)joinaddr)== NULL){ /* Calls ENInit */
#ifdef DEBUGLOG
        LOG(&thisnode->addr, "MPInit failed");
#endif
        exit(1);
    }
#ifdef DEBUGLOG
    else LOG(&thisnode->addr, "MPInit succeeded. Hello.");
#endif

    thisnode->bfailed=0;
    thisnode->inited=1;
    thisnode->ingroup=0;
    thisnode->time=getcurrtime();
    thisnode->hb=0;

    thisnode->memberlist = NULL;
    /* memlist_entry * end_sentinel = malloc(sizeof(memlist_entry)); */
    /* memcpy(&end_sentinel->addr, NULLADDR, sizeof(address)); */
    /* end_sentinel->time = 0; */
    /* end_sentinel->hb = 0; */
    /* end_sentinel->next = NULL; */

    /* memlist_entry * start_sentinel = malloc(sizeof(memlist_entry)); */
    /* memcpy(&start_sentinel->addr, NULLADDR, sizeof(address)); */
    /* start_sentinel->time = 0; */
    /* start_sentinel->hb = 0; */
    /* start_sentinel->next = end_sentinel; */
    
    /* thisnode->memberlist = start_sentinel; */

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
void nodeloopops(member *node){

	/* <your code goes in here> */
    
    return;
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
    if(init_thisnode(node, &joinaddr) == -1){

#ifdef DEBUGLOG
        LOG(&node->addr, "init_thisnode failed. Exit.");
#endif
        exit(1);
    }

	static char stdstring[30];
	sprintf(stdstring, "%d.%d.%d.%d:%d ", node->addr.addr[0], node->addr.addr[1], node->addr.addr[2], node->addr.addr[3], *(short *)&node->addr.addr[4]);
    printf(" %s\n", stdstring); //print new nodes addr


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

