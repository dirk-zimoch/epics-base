

/* * $Id$
 *
 *                    L O S  A L A M O S
 *              Los Alamos National Laboratory
 *               Los Alamos, New Mexico 87545
 *
 *  Copyright, 1986, The Regents of the University of California.
 *
 *  Author: Jeff Hill
 */

#include "iocinf.h"

#include "inetAddrID_IL.h"
#include "bhe_IL.h"
#include "tcpiiu_IL.h"
#include "claimMsgCache_IL.h"
#include "cac_IL.h"
#include "comQueSend_IL.h"
#include "netiiu_IL.h"

const caHdr cacnullmsg = {
    0,0,0,0,0,0
};

// TCP protocol jump table
const tcpiiu::pProtoStubTCP tcpiiu::tcpJumpTableCAC [] = 
{
    tcpiiu::noopAction,
    tcpiiu::eventRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::readRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::exceptionRespAction,
    tcpiiu::clearChannelRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::readNotifyRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::claimCIURespAction,
    tcpiiu::writeNotifyRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::accessRightsRespAction,
    tcpiiu::echoRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::badTCPRespAction,
    tcpiiu::verifyAndDisconnectChan,
    tcpiiu::verifyAndDisconnectChan
};

tsFreeList < class tcpiiu, 16 > tcpiiu::freeList;

#ifdef DEBUG
#   define debugPrintf(argsInParen) printf argsInParen
#else
#   define debugPrintf(argsInParen)
#endif

#ifdef CONVERSION_REQUIRED 
extern CACVRTFUNC *cac_dbr_cvrt[];
#endif /*CONVERSION_REQUIRED*/

const static char nullBuff[32] = {
    0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,
    0,0
};

/*
 * tcpiiu::connect ()
 */
void tcpiiu::connect ()
{
    int status;
        
    /* 
     * attempt to connect to a CA server
     */
    this->armSendWatchdog ();
    while (1) {
        int errnoCpy;

        osiSockAddr addr = this->ipToA.address ();
        status = ::connect ( this->sock, &addr.sa,
                sizeof ( addr.sa ) );
        if ( status == 0 ) {
            this->cancelSendWatchdog ();
            // put the iiu into the connected state
            this->state = iiu_connected;
            // start connection activity watchdog
            this->connectNotify (); 
            return;
        }

        errnoCpy = SOCKERRNO;

        if ( errnoCpy == SOCK_EINTR ) {
            if ( this->state == iiu_disconnected ) {
                return;
            }
            else {
                continue;
            }
        }
        else if ( errnoCpy == SOCK_SHUTDOWN ) {
            return;
        }
        else {  
            this->cancelSendWatchdog ();
            ca_printf ( "Unable to connect because %d=\"%s\"\n", 
                errnoCpy, SOCKERRSTR ( errnoCpy ) );
            this->shutdown ();
            return;
        }
    }
}

/*
 *  cacSendThreadTCP ()
 */
extern "C" void cacSendThreadTCP ( void *pParam )
{
    tcpiiu *piiu = ( tcpiiu * ) pParam;
    claimMsgCache cache ( CA_V44 ( CA_PROTOCOL_VERSION, piiu->minor_version_number ) );

    while ( true ) {

        semBinaryMustTake ( piiu->sendThreadFlushSignal );

        if ( piiu->state != iiu_connected ) {
            break;
        }

        if ( piiu->pClaimsPendingIIU->channelCount () > 0u ) {
            piiu->pClaimsPendingIIU->sendPendingClaims ( *piiu,
                CA_V42 ( CA_PROTOCOL_VERSION, piiu->minor_version_number ), cache );
            piiu->flushPending = true;
        }

        if ( piiu->busyStateDetected != piiu->flowControlActive ) {
            if ( piiu->flowControlActive ) {
                piiu->enableFlowControlMsg ();
            }
            else {
                piiu->disableFlowControlMsg ();
            }
            piiu->flushPending = true;
        }

        if ( piiu->echoRequestPending ) {
            piiu->echoRequestMsg ();
            piiu->flushPending = true;
        }

        if ( piiu->flushPending ) {
            if ( ! piiu->flushToWire () ) {
                break;
            }
            piiu->flushPending = false;
        }
    }

    semBinaryGive ( piiu->sendThreadExitSignal );
}

unsigned tcpiiu::sendBytes ( const void *pBuf, unsigned nBytesInBuf )
{
    int status;

    assert ( nBytesInBuf <= INT_MAX );

    while ( true ) {
        this->clientCtx ().enableCallbackPreemption ();
        this->armSendWatchdog ();
        status = ::send ( this->sock, 
            static_cast < const char * > (pBuf), (int) nBytesInBuf, 0 );
        this->clientCtx ().disableCallbackPreemption ();
        if ( status > 0 ) {
            this->cancelSendWatchdog ();
            return ( unsigned ) status;
        }
        else {
            int localError = SOCKERRNO;

            if ( status == 0 ) {
                this->cancelSendWatchdog ();
                this->shutdown ();
                return 0u;
            }

            if ( localError == SOCK_SHUTDOWN ) {
                this->cancelSendWatchdog ();
                return 0u;
            }

            if ( localError == SOCK_EINTR ) {
                continue;
            }
       
            if ( localError != SOCK_EPIPE && localError != SOCK_ECONNRESET &&
                localError != SOCK_ETIMEDOUT && localError != SOCK_ECONNABORTED ) {
                ca_printf ("CAC: unexpected TCP send error: %s\n", SOCKERRSTR (localError) );
            }

            this->cancelSendWatchdog ();
            this->shutdown ();
            return 0u;
        }
    }
}

unsigned tcpiiu::recvBytes ( void *pBuf, unsigned nBytesInBuf )
{
    unsigned        totalBytes;
    int             status;
    
    if ( this->state != iiu_connected ) {
        return 0u;
    }

    assert ( nBytesInBuf <= INT_MAX );
    status = ::recv ( this->sock, static_cast <char *> ( pBuf ), 
        static_cast <int> ( nBytesInBuf ), 0);
    if ( status <= 0 ) {
        int localErrno = SOCKERRNO;

        if ( status == 0 ) {
            this->shutdown ();
            return 0u;
        }

        if ( localErrno == SOCK_SHUTDOWN ) {
            return 0u;
        }

        if ( localErrno == SOCK_EINTR ) {
            return 0u;
        }
        
        if ( localErrno == SOCK_ECONNABORTED ) {
            return 0u;
        }

        {
            char name[64];
            this->hostName ( name, sizeof (name) );
            ca_printf ( "Disconnecting from CA server %s because: %s\n", 
                name, SOCKERRSTR (localErrno) );
        }

        this->shutdown ();

        return 0u;
    }
    
    assert ( static_cast <unsigned> ( status ) <= nBytesInBuf );
    totalBytes = static_cast <unsigned> ( status );

    if ( nBytesInBuf == totalBytes ) {
        if ( this->contigRecvMsgCount >= contiguousMsgCountWhichTriggersFlowControl ) {
            this->busyStateDetected = true;
        }
        else { 
            this->contigRecvMsgCount++;
        }
    }
    else {
        this->contigRecvMsgCount = 0u;
        this->busyStateDetected = false;
    }

    // care is taken not to post the busy/ready message to the send buffer
    // here so that we avoid push/pull deadlocks
    this->flowControlStateChange = this->busyStateDetected != this->flowControlActive;
    
    this->messageArrivalNotify (); // reschedule connection activity watchdog

    return totalBytes;
}

/*
 *  cacRecvThreadTCP ()
 */
extern "C" void cacRecvThreadTCP (void *pParam)
{
    tcpiiu *piiu = (tcpiiu *) pParam;

    piiu->connect ();
    if ( piiu->state == iiu_connected ) {
        unsigned priorityOfSelf = threadGetPrioritySelf ();
        unsigned priorityOfSend;
        threadBoolStatus tbs;
        threadId tid;

        tbs  = threadLowestPriorityLevelAbove ( priorityOfSelf, &priorityOfSend );
        if ( tbs != tbsSuccess ) {
            priorityOfSend = priorityOfSelf;
        }
        tid = threadCreate ( "CAC-TCP-send", priorityOfSend,
                threadGetStackSize ( threadStackMedium ), cacSendThreadTCP, piiu );
        if ( tid ) {
            while ( piiu->state == iiu_connected ) {
                if ( piiu->comQueRecv::occupiedBytes () >= 0x4000 ) {
                    semBinaryMustTake ( piiu->recvThreadRingBufferSpaceAvailableSignal );
                }
                else {
                    unsigned nBytesIn = piiu->fillFromWire ();
                    if ( nBytesIn ) {
                        piiu->recvPending = true;
                        piiu->clientCtx ().signalRecvActivity ();
                    }
                }
            }
        }
        else {
            semBinaryGive ( piiu->sendThreadExitSignal );
            piiu->shutdown ();
        }
    }
    else {
        semBinaryGive ( piiu->sendThreadExitSignal );
    }
    semBinaryGive ( piiu->recvThreadExitSignal );
}

//
// tcpiiu::tcpiiu ()
//
tcpiiu::tcpiiu ( cac &cac, const osiSockAddr &addrIn, 
        unsigned minorVersion, class bhe &bheIn,
        double connectionTimeout, osiTimerQueue &timerQueue,
        ipAddrToAsciiEngine &engineIn ) :
    tcpRecvWatchdog ( connectionTimeout, timerQueue, CA_V43 (CA_PROTOCOL_VERSION, minorVersion ) ),
    tcpSendWatchdog ( connectionTimeout, timerQueue ),
    netiiu ( cac ),
    ipToA ( addrIn, engineIn ),
    bhe ( bheIn ),
    contigRecvMsgCount ( 0u ),
    busyStateDetected ( false ),
    flowControlActive ( false ),
    flowControlStateChange ( false ),
    recvPending ( false ),
    flushPending ( false ),
    msgHeaderAvailable ( false )
{
    SOCKET newSocket;
    int status;
    int flag;

    newSocket = socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if ( newSocket == INVALID_SOCKET ) {
        ca_printf ("CAC: unable to create virtual circuit because \"%s\"\n",
            SOCKERRSTR (SOCKERRNO));
        this->fullyConstructedFlag = false;
        return;
    }

    //
    // apparently this is nolonger necessary with modern IP kernels
    //
#if 0
    flag = TRUE;
    status = setsockopt ( newSocket, IPPROTO_TCP, TCP_NODELAY,
                (char *)&flag, sizeof(flag) );
    if (status < 0) {
        ca_printf ("CAC: problems setting socket option TCP_NODELAY = \"%s\"\n",
            SOCKERRSTR (SOCKERRNO));
    }
#endif
    flag = TRUE;
    status = setsockopt ( newSocket , SOL_SOCKET, SO_KEEPALIVE,
                (char *)&flag, sizeof (flag) );
    if (status < 0) {
        ca_printf ("CAC: problems setting socket option SO_KEEPALIVE = \"%s\"\n",
            SOCKERRSTR (SOCKERRNO));
    }

#if 0
    {
        int i;

        /*
         * some concern that vxWorks will run out of mBuf's
         * if this change is made joh 11-10-98
         */        
        i = MAX_MSG_SIZE;
        status = setsockopt ( newSocket, SOL_SOCKET, SO_SNDBUF,
                (char *)&i, sizeof (i) );
        if (status < 0) {
            ca_printf ("CAC: problems setting socket option SO_SNDBUF = \"%s\"\n",
                SOCKERRSTR (SOCKERRNO));
        }
        i = MAX_MSG_SIZE;
        status = setsockopt (newSocket, SOL_SOCKET, SO_RCVBUF,
                (char *)&i, sizeof (i) );
        if (status < 0) {
            ca_printf ("CAC: problems setting socket option SO_RCVBUF = \"%s\"\n",
                SOCKERRSTR (SOCKERRNO));
        }
    }
#endif

    this->sock = newSocket;
    this->minor_version_number = minorVersion;
    this->echoRequestPending = false;
    this->curDataMax = 0ul;
    memset ( (void *) &this->curMsg, '\0', sizeof (this->curMsg) );
    this->pCurData = 0;

    /*
     * TCP starts out in the connecting state and later transitions
     * to the connected state 
     */
    this->state = iiu_connecting;

    this->sendThreadExitSignal = semBinaryCreate (semEmpty);
    if ( ! this->sendThreadExitSignal ) {
        ca_printf ("CA: unable to create CA client send thread exit semaphore\n");
        socket_close ( newSocket );
        this->fullyConstructedFlag = false;
        return;
    }

    this->recvThreadExitSignal = semBinaryCreate (semEmpty);
    if ( ! this->recvThreadExitSignal ) {
        ca_printf ("CA: unable to create CA client send thread exit semaphore\n");
        semBinaryDestroy (this->sendThreadExitSignal);
        socket_close ( newSocket );
        this->fullyConstructedFlag = false;
        return;
    }

    this->pClaimsPendingIIU = new claimsPendingIIU ( *this );
    if ( ! this->pClaimsPendingIIU ) {
        ca_printf ("CA: unable to create claimsPendingIIU object\n");
        semBinaryDestroy (this->sendThreadExitSignal);
        socket_close ( newSocket );
        this->fullyConstructedFlag = false;
        return;
    }

    this->sendThreadFlushSignal = semBinaryCreate ( semEmpty );
    if ( ! this->sendThreadFlushSignal ) {
        ca_printf ("CA: unable to create sendThreadFlushSignal object\n");
        semBinaryDestroy (this->sendThreadExitSignal);
        socket_close ( newSocket );
        delete this->pClaimsPendingIIU;
        this->fullyConstructedFlag = false;
        return;
    }

    this->recvThreadRingBufferSpaceAvailableSignal = semBinaryCreate ( semEmpty );
    if ( ! this->recvThreadRingBufferSpaceAvailableSignal ) {
        ca_printf ("CA: unable to create recvThreadRingBufferSpaceAvailableSignal object\n");
        semBinaryDestroy (this->sendThreadExitSignal);
        semBinaryDestroy (this->sendThreadFlushSignal);
        socket_close ( newSocket );
        delete this->pClaimsPendingIIU;
        this->fullyConstructedFlag = false;
        return;
    }

    this->userNameSetMsg ();

    this->hostNameSetMsg ();

    {
        unsigned priorityOfSelf = threadGetPrioritySelf ();
        unsigned priorityOfRecv;
        threadBoolStatus tbs;
        threadId tid;

        tbs  = threadHighestPriorityLevelBelow (priorityOfSelf, &priorityOfRecv);
        if ( tbs != tbsSuccess ) {
            priorityOfRecv = priorityOfSelf;
        }

        tid = threadCreate ("CAC-TCP-recv", priorityOfRecv,
                threadGetStackSize (threadStackMedium), cacRecvThreadTCP, this);
        if ( tid == 0 ) {
            ca_printf ("CA: unable to create CA client receive thread\n");
            semBinaryDestroy (this->recvThreadExitSignal);
            semBinaryDestroy (this->sendThreadExitSignal);
            socket_close ( newSocket );
            delete this->pClaimsPendingIIU;
            semBinaryDestroy (this->sendThreadFlushSignal);
            semBinaryDestroy (this->recvThreadRingBufferSpaceAvailableSignal);
            this->fullyConstructedFlag = false;
            return;
        }
    }

    bhe.bindToIIU ( *this );
    cac.installIIU ( *this );

    this->fullyConstructedFlag = true;
}

/*
 *  tcpiiu::shutdown ()
 */
void tcpiiu::shutdown ()
{
    bool laborNeeded;

    this->lock ();
    if ( this->state != iiu_disconnected ) {
        this->state = iiu_disconnected;
        laborNeeded = true;
    }
    else  {
        laborNeeded = false;
    }
    this->unlock ();

    if ( laborNeeded ) {
        int status = ::shutdown ( this->sock, SD_BOTH );
        if ( status ) {
            errlogPrintf ("CAC TCP shutdown error was %s\n", 
                SOCKERRSTR (SOCKERRNO) );
        }
        semBinaryGive ( this->sendThreadFlushSignal );
        semBinaryGive ( this->recvThreadRingBufferSpaceAvailableSignal );
        this->recvPending = true;
        this->clientCtx ().signalRecvActivity ();
    }
}

//
// tcpiiu::~tcpiiu ()
//
tcpiiu::~tcpiiu ()
{
    if ( ! this->fullyConstructedFlag ) {
        return;
    }

    this->fullyConstructedFlag = false;

    this->shutdown ();

    if ( this->channelCount () ) {
        char hostName[64];
        this->ipToA.hostName ( hostName, sizeof ( hostName ) );
        genLocalExcep ( this->clientCtx (), ECA_DISCONN, hostName );
    }

    this->disconnectAllChan ();

    // wait for send and recv threads to exit
    semBinaryMustTake ( this->sendThreadExitSignal );
    semBinaryMustTake ( this->recvThreadExitSignal );

    if ( this->pClaimsPendingIIU ) {
        delete this->pClaimsPendingIIU;
    }

    semBinaryDestroy ( this->sendThreadExitSignal );
    semBinaryDestroy ( this->recvThreadExitSignal );
    semBinaryDestroy ( this->sendThreadFlushSignal );
    semBinaryDestroy ( this->recvThreadRingBufferSpaceAvailableSignal );

    this->clientCtx ().removeIIU ( *this );

    socket_close ( this->sock );

    /*
     * free message body cache
     */
    if ( this->pCurData ) {
        free ( this->pCurData );
    }
}

void tcpiiu::suicide ()
{
    delete this;
}

bool tcpiiu::connectionInProgress ( const char *pChannelName, const osiSockAddr &addr ) const
{
    if ( ! this->ipToA.identicalAddress ( addr ) ) {
        char acc[64];
        char rej[64];
        char buf[256];

        this->ipToA.hostName ( acc, sizeof (acc) );
        sockAddrToA ( &addr.sa, rej, sizeof (rej) );
        sprintf ( buf, "Channel: \"%.64s\", Connecting to: %.64s, Ignored: %.64s",
                pChannelName, acc, rej );
        genLocalExcep ( this->clientCtx (), ECA_DBLCHNL, buf );
    }
    return true;
}

void tcpiiu::show ( unsigned /* level */ ) const
{
}

void tcpiiu::echoRequest ()
{
    this->echoRequestPending = true;
    this->flush ();
}

/*
 * tcpiiu::echoRequestMsg ()
 */
void tcpiiu::echoRequestMsg ()
{
    if ( CA_V43 ( CA_PROTOCOL_VERSION, this->minor_version_number ) ) {
        this->comQueSend::echoRequest ();
    }
    else {
        this->comQueSend::noopRequest ();
    }

    this->echoRequestPending = false;
}

/*
 *  tcpiiu::enableFlowControlMsg ()
 */
void tcpiiu::enableFlowControlMsg ()
{
    this->comQueSend::enableFlowControlRequest ();
    this->flowControlActive = true;
#   if defined ( DEBUG ) || 1
    printf( "fc on\n" );
#   endif
}

/*
 * tcpiiu::disableFlowControlMsg ()
 */
void tcpiiu::disableFlowControlMsg ()
{
    this->comQueSend::disableFlowControlRequest ();
    this->flowControlActive = false;
#   if defined ( DEBUG ) || 1
        printf ( "fc off\n" );
#   endif
}

/*
 * tcpiiu::hostNameSetMsg ()
 */
void tcpiiu::hostNameSetMsg ()
{
    if ( ! CA_V41 ( CA_PROTOCOL_VERSION, this->minor_version_number ) ) {
        return;
    }

    this->comQueSend::hostNameSetRequest ( localHostNameAtLoadTime.pointer () );
}

/*
 * tcpiiu::userNameSetMsg ()
 */
void tcpiiu::userNameSetMsg ()
{
    if ( ! CA_V41 ( CA_PROTOCOL_VERSION, this->minor_version_number ) ) {
        return;
    }

    this->comQueSend::userNameSetRequest ( this->clientCtx ().userNamePointer () );
}

void tcpiiu::noopAction ()
{
    return;
}
 
void tcpiiu::echoRespAction ()
{
    return;
}

void tcpiiu::writeNotifyRespAction ()
{
    int status = this->curMsg.m_cid;
    if ( status == ECA_NORMAL ) {
        this->clientCtx ().ioCompletionNotifyAndDestroy ( this->curMsg.m_available );
    }
    else {
        this->clientCtx ().ioExceptionNotifyAndDestroy ( this->curMsg.m_available, 
                    status, "write notify request rejected" );
    }
}

void tcpiiu::readNotifyRespAction ()
{
    int v41;
    int status;

    /*
     * convert the data buffer from net
     * format to host format
     */
#   ifdef CONVERSION_REQUIRED 
        if ( this->curMsg.m_dataType < NELEMENTS ( cac_dbr_cvrt ) ) {
            ( *cac_dbr_cvrt[ this->curMsg.m_dataType ] ) (
                 this->pCurData, this->pCurData, FALSE, this->curMsg.m_count);
        }
        else {
            this->curMsg.m_cid = htonl ( ECA_BADTYPE );
        }
#   endif

    /*
     * the channel id field is abused for
     * read notify status starting
     * with CA V4.1
     */
    v41 = CA_V41 ( CA_PROTOCOL_VERSION, this->minor_version_number );
    if (v41) {
        status = this->curMsg.m_cid;
    }
    else{
        status = ECA_NORMAL;
    }

    if ( status == ECA_NORMAL ) {
        this->clientCtx ().ioCompletionNotifyAndDestroy ( this->curMsg.m_available,
            this->curMsg.m_dataType, this->curMsg.m_count, this->pCurData );
    }
    else {
        this->clientCtx ().ioExceptionNotifyAndDestroy ( this->curMsg.m_available,
            status, "read failed", this->curMsg.m_dataType, this->curMsg.m_count );
    }
}

void tcpiiu::eventRespAction ()
{
    int v41;
    int status;

    /*
     * m_postsize = 0 used to be a confirmation, but is
     * now a noop because the above hash lookup will 
     * not find a matching IO block
     */
    if ( ! this->curMsg.m_postsize ) {
        this->clientCtx ().ioDestroy ( this->curMsg.m_available );
        return;
    }

    /*
     * convert the data buffer from net
     * format to host format
     */
#   ifdef CONVERSION_REQUIRED 
        if ( this->curMsg.m_dataType < NELEMENTS ( cac_dbr_cvrt ) ) {
            ( *cac_dbr_cvrt [ this->curMsg.m_dataType ] )(
                 this->pCurData, this->pCurData, FALSE,
                 this->curMsg.m_count);
        }
        else {
            this->curMsg.m_cid = htonl ( ECA_BADTYPE );
        }
#   endif

    /*
     * the channel id field is abused for
     * read notify status starting
     * with CA V4.1
     */
    v41 = CA_V41 ( CA_PROTOCOL_VERSION, this->minor_version_number );
    if (v41) {
        status = this->curMsg.m_cid;
    }
    else {
        status = ECA_NORMAL;
    }
    if ( status == ECA_NORMAL ) {
        this->clientCtx ().ioCompletionNotify ( this->curMsg.m_available,
            this->curMsg.m_dataType, this->curMsg.m_count, this->pCurData );
    }
    else {
        this->clientCtx ().ioExceptionNotify ( this->curMsg.m_available,
                status, "subscription update failed", 
                this->curMsg.m_dataType, this->curMsg.m_count );
    }
}

void tcpiiu::readRespAction ()
{
    this->clientCtx ().ioCompletionNotifyAndDestroy ( this->curMsg.m_available,
        this->curMsg.m_dataType, this->curMsg.m_count, this->pCurData );
}

void tcpiiu::clearChannelRespAction ()
{
    this->clientCtx ().channelDestroy ( this->curMsg.m_available );
}

void tcpiiu::exceptionRespAction ()
{
    char context[255];
    char hostName[64];
    caHdr *req = (caHdr *) this->pCurData;

    this->ipToA.hostName ( hostName, sizeof ( hostName ) );
    if ( this->curMsg.m_postsize > sizeof (caHdr) ) {
        sprintf ( context, "detected by: %s for: %s", 
            hostName, (char *)(req+1) );
    }
    else{
        sprintf ( context, "detected by: %s", hostName );
    }

    switch ( ntohs ( req->m_cmmd ) ) {
    case CA_PROTO_READ_NOTIFY:
        this->clientCtx ().ioExceptionNotifyAndDestroy ( ntohl (req->m_available), 
            ntohl (this->curMsg.m_available), context, 
            ntohs (req->m_dataType), ntohs (req->m_count) );
        break;
    case CA_PROTO_READ:
        this->clientCtx ().ioExceptionNotifyAndDestroy ( ntohl (req->m_available), 
            ntohl (this->curMsg.m_available), context, 
            ntohs (req->m_dataType), ntohs (req->m_count) );
        break;
    case CA_PROTO_WRITE_NOTIFY:
        this->clientCtx ().ioExceptionNotifyAndDestroy ( ntohl (req->m_available), 
            ntohl (this->curMsg.m_available), context, 
            ntohs (req->m_dataType), ntohs (req->m_count) );
        break;
    case CA_PROTO_WRITE:
        this->clientCtx ().exceptionNotify ( ntohl ( this->curMsg.m_available), 
                context, ntohs (req->m_dataType), ntohs (req->m_count), __FILE__, __LINE__);
        break;
    case CA_PROTO_EVENT_ADD:
        this->clientCtx ().ioExceptionNotify ( ntohl (req->m_available), 
            ntohl (this->curMsg.m_available), context, 
            ntohs (req->m_dataType), ntohs (req->m_count) );
        break;
    case CA_PROTO_EVENT_CANCEL:
        this->clientCtx ().ioExceptionNotifyAndDestroy ( ntohl (req->m_available), 
            ntohl (this->curMsg.m_available), context );
        break;
    default:
        this->clientCtx ().exceptionNotify (ntohl (this->curMsg.m_available), 
            context, __FILE__, __LINE__);
        break;
    }
}

void tcpiiu::accessRightsRespAction ()
{
    static caar init;
    caar arBitField = init; // shut up bounds checker
    unsigned ar;

    ar = this->curMsg.m_available;
    arBitField.read_access = ( ar & CA_PROTO_ACCESS_RIGHT_READ ) ? 1 : 0;
    arBitField.write_access = ( ar & CA_PROTO_ACCESS_RIGHT_WRITE ) ? 1 : 0;

    this->clientCtx ().accessRightsNotify ( this->curMsg.m_cid, arBitField );
}

void tcpiiu::claimCIURespAction ()
{
    this->clientCtx ().connectChannel ( this->curMsg.m_cid, *this, 
        this->curMsg.m_dataType, this->curMsg.m_count, this->curMsg.m_available );
}

void tcpiiu::verifyAndDisconnectChan ()
{
    this->clientCtx ().disconnectChannel ( this->curMsg.m_cid );
}

void tcpiiu::badTCPRespAction ()
{
    char hostName[64];
    this->ipToA.hostName ( hostName, sizeof ( hostName ) );
    ca_printf ( "CAC: Bad response code in TCP message from %s was %u\n", 
        hostName, this->curMsg.m_cmmd);
}

/*
 * post_tcp_msg ()
 */
void tcpiiu::postMsg ()
{
    while ( 1 ) {

        //
        // fetch a complete message header
        //
        if ( ! this->msgHeaderAvailable ) {
            this->msgHeaderAvailable = this->copyOutBytes ( &this->curMsg, sizeof ( this->curMsg ) );
            if ( ! this->msgHeaderAvailable ) {
                return;
            }
            
            semBinaryGive ( this->recvThreadRingBufferSpaceAvailableSignal );

            //
            // fix endian of bytes 
            //
            this->curMsg.m_cmmd = ntohs ( this->curMsg.m_cmmd );
            this->curMsg.m_postsize = ntohs ( this->curMsg.m_postsize );
            this->curMsg.m_dataType = ntohs ( this->curMsg.m_dataType );
            this->curMsg.m_count = ntohs ( this->curMsg.m_count );
            this->curMsg.m_cid = ntohl ( this->curMsg.m_cid );
            this->curMsg.m_available = ntohl ( this->curMsg.m_available );
#if 0
            ca_printf (
                "%s Cmd=%3d Type=%3d Count=%4d Size=%4d",
                this->host_name_str,
                this->curMsg.m_cmmd,
                this->curMsg.m_dataType,
                this->curMsg.m_count,
                this->curMsg.m_postsize);
            ca_printf (
                " Avail=%8x Cid=%6d\n",
                this->curMsg.m_available,
                this->curMsg.m_cid);
#endif
        }

        //
        // dont allow huge msg body until
        // the client library supports it
        //
        if ( this->curMsg.m_postsize > (unsigned) MAX_TCP ) {
            this->msgHeaderAvailable = false;
            ca_printf ("CAC: message body was too large (disconnecting)\n");
            this->shutdown ();
            return;
        }

        //
        // make sure we have a large enough message body cache
        //
        if ( this->curMsg.m_postsize > this->curDataMax ) {
            void *pData;
            size_t cacheSize;

            /* 
             * scalar DBR_STRING is sometimes clipped to the
             * actual string size so make sure this cache is
             * as large as one DBR_STRING so they will
             * not page fault if they read MAX_STRING_SIZE
             * bytes (instead of the actual string size).
             */
            cacheSize = max ( this->curMsg.m_postsize * 2u, MAX_STRING_SIZE );
            pData = (void *) calloc (1u, cacheSize);
            if ( ! pData ) {
                ca_printf ("CAC: not enough memory for message body cache (disconnecting)\n");
                this->shutdown ();
                return;
            }
            if (this->pCurData) {
                free (this->pCurData);
            }
            this->pCurData = pData;
            this->curDataMax = this->curMsg.m_postsize;
        }

        bool msgBodyAvailable = this->copyOutBytes ( this->pCurData, this->curMsg.m_postsize );
        if ( ! msgBodyAvailable ) {
            return;
        }

        semBinaryGive ( this->recvThreadRingBufferSpaceAvailableSignal );

        /*
         * execute the response message
         */
        pProtoStubTCP pStub;
        if ( this->curMsg.m_cmmd >= NELEMENTS ( tcpJumpTableCAC ) ) {
            pStub = badTCPRespAction;
        }
        else {
            pStub = tcpJumpTableCAC [this->curMsg.m_cmmd];
        }
        ( this->*pStub ) ();
         
        this->msgHeaderAvailable = false;
    }
}

inline int tcpiiu::requestStubStatus ()
{
    if ( this->state == iiu_connected ) {
        return ECA_NORMAL;
    }
    else {
        return ECA_DISCONNCHID;
    }
}

int tcpiiu::writeRequest ( unsigned serverId, unsigned type, unsigned nElem, const void *pValue )
{
    return this->comQueSend::writeRequest ( serverId, type,  nElem,  pValue );
}

int tcpiiu::writeNotifyRequest ( unsigned ioId, unsigned serverId, unsigned type, unsigned nElem, const void *pValue )
{
    if ( this->ca_v41_ok () ) {
        return this->comQueSend::writeNotifyRequest ( ioId, serverId, type,  nElem,  pValue );
    }
    else {
        return ECA_NOSUPPORT;
    }
}

int tcpiiu::readCopyRequest ( unsigned ioId, unsigned serverId, unsigned type, unsigned nElem )
{
    return this->comQueSend::readCopyRequest ( ioId, serverId, type,  nElem );
}

int tcpiiu::readNotifyRequest ( unsigned ioId, unsigned serverId, unsigned type, unsigned nElem )
{
    return this->comQueSend::readNotifyRequest ( ioId, serverId, type,  nElem );
}

int tcpiiu::createChannelRequest ( unsigned clientId, const char *pName, unsigned nameLength )
{
    return this->comQueSend::createChannelRequest ( clientId, pName, nameLength );
}

int tcpiiu::clearChannelRequest ( unsigned clientId, unsigned serverId )
{
    return this->comQueSend::clearChannelRequest ( clientId, serverId );
}

int tcpiiu::subscriptionRequest ( unsigned ioId, unsigned serverId, unsigned type, unsigned nElem, unsigned mask )
{
    return this->comQueSend::subscriptionRequest ( ioId, serverId, type, nElem, mask );
}

int tcpiiu::subscriptionCancelRequest ( unsigned ioId, unsigned serverId, unsigned type, unsigned nElem )
{
    return this->comQueSend::subscriptionCancelRequest ( ioId, serverId, type, nElem );
}

void tcpiiu::processIncomingAndDestroySelfIfDisconnected ()
{
    if ( this->state == iiu_disconnected ) {
        this->suicide ();
    }
    else if ( this->recvPending ) {
        this->recvPending = false;
        this->postMsg ();
    }
}

void tcpiiu::lastChannelDetachNotify ()
{
    this->shutdown ();
}

void tcpiiu::installChannelPendingClaim ( nciu &chan )
{
    chan.attachChanToIIU ( *this->pClaimsPendingIIU );
    // wake up send thread which sends claim message
    semBinaryGive ( this->sendThreadFlushSignal );
}

// the recv thread is not permitted to flush as this
// can result in a push / pull deadlock on the TCP pipe.
// Instead the recv thread scheduals the flush with the 
// send thread which runs at a higher priority than the 
// send thread.
bool tcpiiu::flushToWirePermit ()
{
    if ( this->clientCtx ().currentThreadIsRecvProcessThread () ) {
        this->flushPending = true;
        semBinaryGive ( this->sendThreadFlushSignal );
        return false;
    }
    else {
        return true;
    }
}


