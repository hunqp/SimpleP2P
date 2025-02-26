#ifndef PEER_CONNECTION_H
#define PEER_CONNECTION_H

#include <pthread.h>

#include "main.h"
#include "ults.h"

typedef enum {
    INITITAL,
    INPROGRESS,
    COMPLTED,
    FAILED,
    CLOSED,
} GATHERING_STATE;

typedef enum {
    SUBSCRIBER,
    PUBLISHER,
} ROLE_REGISTRY;

typedef struct {
    int fPort;
    std::string fPublicIP;
    std::string fPrivateIP;
} Candidate;

class PeerConnection {
public:
    PeerConnection(ROLE_REGISTRY role);
    ~PeerConnection();

    GATHERING_STATE gatheringState();
    Candidate gatherLocalCandidates();
    void selectedCaindidatePair(Candidate * remoteCandiate);

    int sendTo(uint8_t *data, uint32_t dataLen);
    int readFrom(uint8_t *data, uint32_t dataLen);

private:
    int createSocket();
    static void * iteratePunchingHole(void *);

private:
    int fFd = -1;
    socklen_t fLen;
    struct sockaddr_in addr;
    struct sockaddr_in addrRemote;

    Candidate fCandidate;
    GATHERING_STATE fState;
    ROLE_REGISTRY fRegister;

    pthread_mutex_t fMutex;
};

#endif /* PEER_CONNECTION_H */