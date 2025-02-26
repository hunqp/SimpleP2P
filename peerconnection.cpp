#include "peerconnection.h"

PeerConnection::PeerConnection(ROLE_REGISTRY role) {
    if (createSocket() < 0) {
        perror("createSocket()\r\n");
    }
    fRegister = role;
    fState = INITITAL;
    fMutex = PTHREAD_MUTEX_INITIALIZER;
}

PeerConnection::~PeerConnection() {
    close(fFd);
    pthread_mutex_destroy(&fMutex);
}

GATHERING_STATE PeerConnection::gatheringState() {
    pthread_mutex_lock(&fMutex);

    auto ret = fState;

    pthread_mutex_unlock(&fMutex);

    return ret;
}

void PeerConnection::selectedCaindidatePair(Candidate * remoteCandiate) {
    if (!remoteCandiate) {
        return;
    }
    
    pthread_mutex_lock(&fMutex);

    addrRemote.sin_family = AF_INET;
    addrRemote.sin_port = htons(remoteCandiate->fPort);

    /**
     * Both Peers are behind different NAT, so we need to make the router 
     * mapped local port into public port
     */
    if (fCandidate.fPublicIP != remoteCandiate->fPublicIP) {
        addrRemote.sin_addr.s_addr = inet_addr(remoteCandiate->fPublicIP.c_str());
        printf("Both Peers are behind different NAT\r\n");
    }
    /* -- Both Peers are behind the same NAT -- */
    else {
        /* -- Both Peers are running on the same machine -- */
        if (fCandidate.fPrivateIP == remoteCandiate->fPrivateIP) {
            addrRemote.sin_addr.s_addr = inet_addr("127.0.0.1");
            printf("Both Peers are behind the same NAT and the same machine\r\n");
        }
        /* -- Both Peers are running on different machine -- */
        else {
            addrRemote.sin_addr.s_addr = inet_addr(remoteCandiate->fPrivateIP.c_str());
            printf("Both Peers are behind the same NAT and different machine\r\n");
        }
    }
    fState = INPROGRESS;

    pthread_mutex_unlock(&fMutex);

    pthread_t createId;
    pthread_create(&createId, NULL, PeerConnection::iteratePunchingHole, this);
}

Candidate PeerConnection::gatherLocalCandidates() {
    pthread_mutex_lock(&fMutex);

    char address[32];

    /* -- STUN Protocol to get IP Public -- */
    memset(address, 0, sizeof(address));
    getPublicIPAddress(ourSTUN, address);
    fCandidate.fPublicIP = std::string(address);

    /* -- Get IP Local -- */
    memset(address, 0, sizeof(address));
    GetPrivateIPAddress(address);
    fCandidate.fPrivateIP = std::string(address);

    fCandidate.fPort = ntohs(addr.sin_port);

    pthread_mutex_unlock(&fMutex);

    return fCandidate;
}

int PeerConnection::sendTo(uint8_t *data, uint32_t dataLen) {
    pthread_mutex_lock(&fMutex);

    socklen_t addrRemoteLen = sizeof(addrRemote);
    int ret = sendto(fFd, data, dataLen, 0, (struct sockaddr *)&addrRemote, addrRemoteLen);

    pthread_mutex_unlock(&fMutex);

    return ret;
}

int PeerConnection::readFrom(uint8_t *data, uint32_t dataLen) {
    pthread_mutex_lock(&fMutex);

    socklen_t addrRemoteLen = sizeof(addrRemote);
    int ret = recvfrom(fFd, data, dataLen, 0, (struct sockaddr *)&addrRemote, &addrRemoteLen);

    pthread_mutex_unlock(&fMutex);

    return ret;
}

int PeerConnection::createSocket() {
    fFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fFd < 0) {
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; /* Let OS (Operating system) assign a port */

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 500000;
    setsockopt(fFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (bind(fFd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr))) {
        return -1;
    }
    socklen_t addrLen = sizeof(addr);
    getsockname(fFd, (struct sockaddr*)&addr, &addrLen);

    return 0;
}

void * PeerConnection::iteratePunchingHole(void *arg) {
    #define PUNCHING_COMMAND_REQ        (char*)"UDP_HOLE_PUNCHING_REQ"
    #define PUNCHING_COMMAND_RES        (char*)"UDP_HOLE_PUNCHING_RES"

    char chars[32];
    PeerConnection *pc = (PeerConnection *)arg;

    while (pc->gatheringState() != COMPLTED) {
        switch (pc->fRegister) {
        case SUBSCRIBER: {
            pc->sendTo((uint8_t*)PUNCHING_COMMAND_REQ, strlen(PUNCHING_COMMAND_REQ));
            memset(chars, 0, sizeof(chars));
            if (pc->readFrom((uint8_t*)chars, sizeof(chars)) > 0) {
                if (strcmp(chars, PUNCHING_COMMAND_RES) == 0) {
                    pc->fState = COMPLTED;
                    printf("[SUBSCRIBER] COMPLTED\r\n");
                }
            }
            else perror("readFrom()");
        }
        break;

        case PUBLISHER: {
            memset(chars, 0, sizeof(chars));
            if (pc->readFrom((uint8_t*)chars, sizeof(chars)) > 0) {
                if (strcmp(chars, PUNCHING_COMMAND_REQ) == 0) {
                    pc->sendTo((uint8_t*)PUNCHING_COMMAND_RES, strlen(PUNCHING_COMMAND_RES));
                    pc->fState = COMPLTED;
                    printf("[PUBLISHER] COMPLTED\r\n");
                }
            }
            else perror("readFrom()");
        }
        break;
        
        default:
        break;
        }
    }

    pthread_detach(pthread_self());
    return NULL;
}