#include "main.h"
#include "json.hpp"
#include "STUNExternalIP.h"
#include "peerconnection.h"

static bool bLoop = true;

static std::unordered_map<std::string, std::shared_ptr<PeerConnection>> clients;

const struct STUNServer ourSTUN = { 
    configSTUNSERVER, 
    configSTUNPORT
};

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
        std::string PubIP = msg["ip_public"].get<std::string>();
        std::string PrvIP = msg["ip_private"].get<std::string>();

        printf("-- PEER CONNECTION --\r\n");
        printf("Port      : %d\r\n", port);
        printf("Machine   : %s\r\n", name.c_str());
        printf("IP Public : %s\r\n", PubIP.c_str());
        printf("IP Private: %s\r\n\r\n", PrvIP.c_str());

        Candidate remote;
        remote.fPort = port;
        remote.fPublicIP = PubIP;
        remote.fPrivateIP = PrvIP;

        std::shared_ptr<PeerConnection> pc = std::make_shared<PeerConnection>(PUBLISHER);
        Candidate candidate = pc->gatherLocalCandidates();

        msg.clear();
        msg = {
            {"name"         , name                  },
            {"port"         , candidate.fPort       },
            {"ip_public"    , candidate.fPublicIP   },
            {"ip_private"   , candidate.fPrivateIP  },
        };
        mosquitto_publish(mosq, NULL, configSIGNALING_TOPIC_RES, msg.dump().length(), msg.dump().c_str(), 0, 0);

        pc->selectedCaindidatePair(&remote);
        clients.emplace(name, pc);
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
    mosquitto_subscribe(mosq, NULL, configSIGNALING_TOPIC_REQ, 0);
    mosquitto_loop_start(mosq);
    LOGP("[CONNECTED] Broker at %s:%d\n", configSIGNALINGSERVER, configSIGNALINGPORT);

    char chars[32];

    while (bLoop) {
        for (auto &it : clients) {
            auto id = it.first;
            auto pc = it.second;

            if (pc->gatheringState() == COMPLTED) {
                memset(chars, 0, sizeof(chars));
                if (pc->readFrom((uint8_t*)chars, sizeof(chars)) > 0) {
                    printf("Received from \"%s\" message \"%s\" \r\n", id.c_str(), chars);

                    char *pong = (char*)"PINGRES";
                    pc->sendTo((uint8_t*)pong, strlen(pong));
                }
            }
        }

        sleep(1);
    }

    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
