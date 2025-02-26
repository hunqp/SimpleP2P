#include "main.h"
#include "json.hpp"
#include "STUNExternalIP.h"
#include "peerconnection.h"

static bool bLoop = true;

static std::string machine;
static PeerConnection *pc = NULL;

const struct STUNServer ourSTUN = { 
    configSTUNSERVER, 
    configSTUNPORT
};

static std::string randomId(size_t length) {
	using std::chrono::high_resolution_clock;
	static thread_local std::mt19937 rng(
	    static_cast<unsigned int>(high_resolution_clock::now().time_since_epoch().count()));
	static const std::string characters(
	    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz");
	std::string id(length, '0');
	std::uniform_int_distribution<int> uniform(0, int(characters.size() - 1));
	std::generate(id.begin(), id.end(), [&]() { return characters.at(uniform(rng)); });
	return id;
}

static void sigProc(int sig) {
    bLoop = false;
    LOGP("[CAUGHT] Signal %d\n", sig);
    exit(EXIT_SUCCESS);
}

static void onSignalingMessage(struct mosquitto *mosq, void *arg, const struct mosquitto_message *message) {
    nlohmann::json msg = nlohmann::json::parse((char *)message->payload);

    try {
        int port = msg["port"].get<int>();
        std::string name = msg["name"].get<std::string>();
        std::string ippublic = msg["ip_public"].get<std::string>();
        std::string ipprivate = msg["ip_private"].get<std::string>();

        if (name == machine) {
            printf("-- FROM PUBLISHER --\r\n");
            printf("Port      : %d\r\n", port);
            printf("Machine   : %s\r\n", name.c_str());
            printf("IP Public : %s\r\n", ippublic.c_str());
            printf("IP Private: %s\r\n\r\n", ipprivate.c_str());
            
            Candidate remote;
            remote.fPort = port;
            remote.fPublicIP = ippublic;
            remote.fPrivateIP = ipprivate;

            if (pc) {
                pc->selectedCaindidatePair(&remote);
            }
        }
    }
    catch(const std::exception& e) {
        LOGE("%s\n", e.what());
    }
}

int main() {
    signal(SIGINT, 	sigProc);
	signal(SIGQUIT, sigProc);

    /* ===========================// CONNECT TO SIGNALING SERVER \\=========================== */
    struct mosquitto *mosq = NULL;

    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        exit(EXIT_FAILURE);
    }
    mosquitto_message_callback_set(mosq, onSignalingMessage);
    int rc = mosquitto_connect(mosq, configSIGNALINGSERVER, configSIGNALINGPORT, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOGP("Can't connect to %s:%d\n", configSIGNALINGSERVER, configSIGNALINGPORT);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        exit(EXIT_FAILURE);
    }
    mosquitto_subscribe(mosq, NULL, configSIGNALING_TOPIC_RES, 0);
    mosquitto_loop_start(mosq);
    LOGP("[CONNECTED] Broker at %s:%d\n", configSIGNALINGSERVER, configSIGNALINGPORT);

    machine = randomId(5);
    pc = new PeerConnection(SUBSCRIBER);
    Candidate candidate = pc->gatherLocalCandidates();

    printf("[CREATE] %s (%s:%d)\r\n", machine.c_str(), candidate.fPublicIP.c_str(), candidate.fPort);
    
    const nlohmann::json msg = {
        {"name"         , machine               },
        {"port"         , candidate.fPort       },
        {"ip_public"    , candidate.fPublicIP   },
        {"ip_private"   , candidate.fPrivateIP  },
    };
    mosquitto_publish(mosq, NULL, configSIGNALING_TOPIC_REQ, msg.dump().length(), msg.dump().c_str(), 0, 0);

    uint8_t Frame[1024 * 200];

    while (bLoop) {

        if (pc->gatheringState() == COMPLTED) {
            memset(Frame, 0, sizeof(Frame));
            int ret = pc->readFrom(Frame, sizeof(Frame));
            if (ret > 0) {
                printf("Received from \"publisher\" on message: \"%s\" \r\n", (char*)Frame);
            }

            char *ping = (char*)"PING";
            pc->sendTo((uint8_t*)ping, strlen(ping));
        }

        sleep(1);
    }

    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
