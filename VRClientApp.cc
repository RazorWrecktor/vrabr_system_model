//
// Copyright (C) 2004 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#include "inet/applications/tcpapp/VRClientApp1.h"

#include "inet/applications/tcpapp/GenericAppMsg_m.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/TimeTag_m.h"
#include "inet/common/lifecycle/ModuleOperations.h"
#include "inet/common/packet/Packet.h"

#include <fstream>
#include <sstream>
#include <vector>
#include <omnetpp.h>

namespace inet {

#define MSGKIND_CONNECT    0
#define MSGKIND_SEND       1
#define MSGKIND_PLAYBACK   2

Define_Module(VRClientApp1);

VRClientApp1::~VRClientApp1()
{
    cancelAndDelete(timeoutMsg);
}

void VRClientApp1::initialize(int stage)
{
    TcpAppBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        videoDuration = 60;
        numRequestsToSend = videoDuration;
        earlySend = false; // TODO make it parameter
        WATCH(numRequestsToSend);
        WATCH(earlySend);

        bufferSize = 0; // current size of the buffer
        int playbackPointer = 0;
        bool videoIsPlaying = false;
        bool videoIsBuffering = true;

//        long requestLength = par("requestLength");
//        long replyLength; // MUST GET FROM RL OUTPUT

        startTime = par("startTime");
        stopTime = par("stopTime");
        if (stopTime >= SIMTIME_ZERO && stopTime < startTime)
            throw cRuntimeError("Invalid startTime/stopTime parameters");
        timeoutMsg = new cMessage("timer");
    }
}

void VRClientApp1::handleStartOperation(LifecycleOperation *operation)
{
    simtime_t now = simTime();
    simtime_t start = std::max(startTime, now);
    if (timeoutMsg && ((stopTime < SIMTIME_ZERO) || (start < stopTime) || (start == stopTime && startTime == stopTime))) {
        timeoutMsg->setKind(MSGKIND_CONNECT);
        scheduleAt(start, timeoutMsg);
    }
}

void VRClientApp1::handleStopOperation(LifecycleOperation *operation)
{
    cancelEvent(timeoutMsg);
    if (socket.getState() == TcpSocket::CONNECTED || socket.getState() == TcpSocket::CONNECTING || socket.getState() == TcpSocket::PEER_CLOSED)
        close();
}

void VRClientApp1::handleCrashOperation(LifecycleOperation *operation)
{
    cancelEvent(timeoutMsg);
    if (operation->getRootModule() != getContainingNode(this))
        socket.destroy();
}

void VRClientApp1::sendRequest()
{
    requestLength = par("requestLength");
    replyLength = par("replyLength"); // MUST GET FROM RL OUTPUT

    // Read CSV and find appropriate replyLength
    try {
        // Parameters you might want to make configurable
        std::string csvFilename = "video1_8333333.csv";
        
        // Read CSV file
        std::ifstream file(csvFilename);
        if (!file.is_open()) {
            throw cRuntimeError("Cannot open CSV file: %s", csvFilename.c_str());
        }
        
        // Skip header
        std::string line;
        std::getline(file, line);

        double targetTime = videoDuration + 1 - numRequestsToSend;
        bool found = false;

        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string timeStr, sizeStr;

            std::getline(ss, timeStr, ',');
            std::getline(ss, sizeStr, ',');
            
            double currentTime = std::stod(timeStr);
            if (currentTime == targetTime) {
                replyLength = std::stol(sizeStr);
                found = true;
                break;
            }
        }

        if (!found) {
            EV_WARN << "No matching Time found in CSV for targetTime=" << targetTime
                   << ". Using default replyLength=" << replyLength << endl;
        }
    }
    catch (const std::exception& e) {
        EV_WARN << "Error processing CSV file: " << e.what()
               << ". Using default replyLength=" << replyLength << endl;
    }

    if (requestLength < 1)
        requestLength = 1;
    if (replyLength < 1)
        replyLength = 1;

    const auto& payload = makeShared<GenericAppMsg>();
    Packet *packet = new Packet("data");
    payload->setChunkLength(B(requestLength));
    payload->setExpectedReplyLength(B(replyLength));
    payload->setServerClose(false);
    payload->addTag<CreationTimeTag>()->setCreationTime(simTime());
    packet->insertAtBack(payload);

    EV_INFO << "nima" << endl;
    EV_INFO << "sending request with " << requestLength << " bytes, expected reply length " << replyLength << " bytes,"
            << "remaining " << numRequestsToSend - 1 << " request\n";

    sendPacket(packet);
}

void VRClientApp1::handleTimer(cMessage *msg)
{
    switch (msg->getKind()) {
        case MSGKIND_CONNECT:
            connect(); // active OPEN

            // significance of earlySend: if true, data will be sent already
            // in the ACK of SYN, otherwise only in a separate packet (but still
            // immediately)
            if (earlySend)
                sendRequest();
            break;

        case MSGKIND_SEND:
            sendRequest();
            numRequestsToSend--;
            // no scheduleAt(): next request will be sent when reply to this one
            // arrives (see socketDataArrived())
            break;

        case MSGKIND_PLAYBACK:
            if (playbackPointer == 0) {
                videoIsPlaying = true;
            }
            playbackPointer++;
            recordToCSV(playbackPointer, simTime(), "playbackPointer.csv");
            EV_INFO << "case: MSGKIND_VIDEO_PLAY\n";
            EV_INFO << "videoIsPlaying = " << videoIsPlaying << "\n";
            EV_INFO << "videoIsBuffering = " << videoIsBuffering << "\n";
            EV_INFO << "numRequestsToSend = " << numRequestsToSend << "\n";
            if (playbackPointer >= videoDuration) {
                EV_INFO << "Video playback complete. Stopping streaming.";
                cancelAndDelete(msg);
                videoIsPlaying = false;
                videoIsBuffering = false;
                return;
            }
            EV_INFO << "----------> Video play event\n";
            cancelAndDelete(msg);
            if (bufferSize > 0) {
                simtime_t d = simTime() + 1.0;
                cMessage *videoPlaybackMsg = new cMessage("playback");
                videoPlaybackMsg->setKind(MSGKIND_PLAYBACK);
                scheduleAt(d, videoPlaybackMsg);
            }
            if (!videoIsBuffering && numRequestsToSend > 0) {
                EV_INFO << "videoIsBuffering = false && numRequestsToSend > 0\n";
                videoIsBuffering = true;
                simtime_t d = 1.0;
                rescheduleAfterOrDeleteTimer(d, MSGKIND_SEND);
            }

        default:
            throw cRuntimeError("Invalid timer msg: kind=%d", msg->getKind());
    }
}

void VRClientApp1::socketEstablished(TcpSocket *socket)
{
    TcpAppBase::socketEstablished(socket);

    // determine number of requests in this session
//    numRequestsToSend = par("numRequestsPerSession");
    numRequestsToSend = videoDuration;
    if (numRequestsToSend < 1)
        numRequestsToSend = 1;

    // perform first request if not already done (next one will be sent when reply arrives)
    if (!earlySend)
        sendRequest();

    numRequestsToSend--;
}

void VRClientApp1::rescheduleAfterOrDeleteTimer(simtime_t d, short int msgKind)
{
    if (stopTime < SIMTIME_ZERO || simTime() + d < stopTime) {
        timeoutMsg->setKind(msgKind);
        rescheduleAfter(d, timeoutMsg);
    }
    else {
        cancelAndDelete(timeoutMsg);
        timeoutMsg = nullptr;
    }
}

void VRClientApp1::socketDataArrived(TcpSocket *socket, Packet *msg, bool urgent)
{
    TcpAppBase::socketDataArrived(socket, msg, urgent);

    accumulatedReplyLength = bytesRcvd - prevBytesRcvd;

    if (numRequestsToSend > 0) {
        EV_INFO << "reply arrived\n";

        if (accumulatedReplyLength >= replyLength) {
            prevBytesRcvd = bytesRcvd;
            bufferSize++;
            recordToCSV(bufferSize, simTime(), "bufferSize.csv");
        }

        if (timeoutMsg) {
            simtime_t d = par("thinkTime");
            rescheduleAfterOrDeleteTimer(d, MSGKIND_SEND);
        }
    }
    else if (socket->getState() != TcpSocket::LOCALLY_CLOSED) {
        EV_INFO << "reply to last request arrived, closing session\n";
        close();
    }
}

void VRClientApp1::close()
{
    TcpAppBase::close();
    cancelEvent(timeoutMsg);
}

void VRClientApp1::socketClosed(TcpSocket *socket)
{
    TcpAppBase::socketClosed(socket);

    // start another session after a delay
    if (timeoutMsg) {
        simtime_t d = par("idleInterval");
        rescheduleAfterOrDeleteTimer(d, MSGKIND_CONNECT);
    }
}

void VRClientApp1::socketFailure(TcpSocket *socket, int code)
{
    TcpAppBase::socketFailure(socket, code);

    // reconnect after a delay
    if (timeoutMsg) {
        simtime_t d = par("reconnectInterval");
        rescheduleAfterOrDeleteTimer(d, MSGKIND_CONNECT);
    }
}

void VRClientApp1::recordToCSV(int metric, const omnetpp::simtime_t& time, const std::string& csvfile) {
    std::ofstream outFile(csvfile, std::ios::app);
    if (!outFile) return;

    if (outFile.tellp() == 0) {
        outFile << "Time,Value\n";
    }

    // Convert simtime_t to double when writing
    outFile << time.dbl() << "," << metric << "\n";
}

} // namespace inet

