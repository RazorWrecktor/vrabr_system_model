//
// Copyright (C) 2004 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __INET_VRCLIENTAPP_H
#define __INET_VRCLIENTAPP_H

#include "inet/applications/tcpapp/TcpAppBase.h"
#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"

namespace inet {

/**
 * An example request-reply based client application.
 */
class INET_API VRClientApp1 : public TcpAppBase
{
  protected:
    cMessage *timeoutMsg = nullptr;
    bool earlySend = false; // if true, don't wait with sendRequest() until established()
    int numRequestsToSend = 0; // requests to send in this session
    simtime_t startTime;
    simtime_t stopTime;

    long requestLength;
    long replyLength; // MUST GET FROM RL OUTPUT

    int bufferSize; // current size of the buffer
    int bufferSizeMax = 10; // maximum size of the buffer
    int videoDuration = 60;
    int playbackPointer;
    bool videoIsPlaying;
    bool videoIsBuffering;
    int currentRequestNumber;
    int prevBytesRcvd = 0;
    long accumulatedReplyLength;

    virtual void sendRequest();
    virtual void rescheduleAfterOrDeleteTimer(simtime_t d, short int msgKind);

    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleTimer(cMessage *msg) override;

    virtual void socketEstablished(TcpSocket *socket) override;
    virtual void socketDataArrived(TcpSocket *socket, Packet *msg, bool urgent) override;
    virtual void socketClosed(TcpSocket *socket) override;
    virtual void socketFailure(TcpSocket *socket, int code) override;

    virtual void handleStartOperation(LifecycleOperation *operation) override;
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;

    virtual void close() override;

    virtual void recordToCSV(int metric, const omnetpp::simtime_t& time, const std::string& csvfile);

  public:
    VRClientApp1() {}
    virtual ~VRClientApp1();
};

} // namespace inet

#endif

