//
//                  Simu5G
//
// Authors: Giovanni Nardini, Giovanni Stea, Antonio Virdis (University of Pisa)
//
// This file is part of a software released under the license included in file
// "license.pdf". Please read LICENSE and README files before using it.
// The above files and the present reference are part of the software itself,
// and cannot be removed from it.
//

#include "stack/backgroundTrafficGenerator/BackgroundTrafficManager.h"
#include "stack/mac/layer/LteMacEnb.h"
#include "stack/phy/layer/LtePhyEnb.h"
#include "stack/phy/ChannelModel/LteChannelModel.h"

Define_Module(BackgroundTrafficManager);

BackgroundTrafficManager::BackgroundTrafficManager()
{
    channelModel_ = nullptr;
}

void BackgroundTrafficManager::initialize(int stage)
{
    cSimpleModule::initialize(stage);
    if (stage == inet::INITSTAGE_LOCAL)
    {
        numBgUEs_ = getAncestorPar("numBgUes");
    }
    if (stage == inet::INITSTAGE_PHYSICAL_ENVIRONMENT)
    {
        // create vector of BackgroundUEs
        for (int i=0; i < numBgUEs_; i++)
            bgUe_.push_back(check_and_cast<TrafficGeneratorBase*>(getParentModule()->getSubmodule("bgUE", i)->getSubmodule("generator")));
    }
    if (stage == inet::INITSTAGE_PHYSICAL_LAYER)
    {
        // get the reference to the MAC layer
        mac_ = check_and_cast<LteMacEnb*>(getParentModule()->getParentModule()->getSubmodule("mac"));
    }
}

void BackgroundTrafficManager::notifyBacklog(int index, Direction dir)
{
    if (dir != DL && dir != UL)
        throw cRuntimeError("TrafficGeneratorBase::consumeBytes - unrecognized direction: %d" , dir);

    backloggedBgUes_[dir].push_back(index);
}

Cqi BackgroundTrafficManager::computeCqi(Direction dir, inet::Coord bgUePos, double bgUeTxPower)
{
    if (channelModel_ == nullptr)
    {
        // get the reference to the channel model for the given carrier
        LtePhyEnb* phy = check_and_cast<LtePhyEnb*>(getParentModule()->getParentModule()->getSubmodule("phy"));
        enbTxPower_ = phy->getTxPwr();
        channelModel_ = phy->getChannelModel(carrierFrequency_);
        if (channelModel_ == nullptr)
            throw cRuntimeError("BackgroundTrafficManager::getCqi - cannot find channel model for carrier frequency %f", carrierFrequency_);
    }

    // this is a fictitious frame that needs to compute the SINR
    LteAirFrame *frame = new LteAirFrame("bgUeSinrComputationFrame");
    UserControlInfo *cInfo = new UserControlInfo();

    // build a control info
    cInfo->setSourceId(BGUE_MIN_ID);  // unique ID for bgUes
    cInfo->setDestId(mac_->getMacNodeId());  // ID of the e/gNodeB
    cInfo->setFrameType(FEEDBACKPKT);
    cInfo->setCoord(bgUePos);
    cInfo->setDirection(dir);
    if (dir == UL)
        cInfo->setTxPower(bgUeTxPower);
    else
        cInfo->setTxPower(enbTxPower_);

    std::vector<double> snr = channelModel_->getSINR_bgUe(frame, cInfo);

    // free memory
    delete frame;
    delete cInfo;


    // convert the SNR to CQI and compute the mean
    Cqi bandCqi, meanCqi = 0;
    std::vector<double>::iterator it = snr.begin();
    for (; it != snr.end(); ++it)
    {
        // TODO implement a lookup table that associates the SINR to a
        //      range of CQI values. Then extract a random number within
        //      that range
        bandCqi = intuniform(2,15);

        meanCqi += bandCqi;
    }
    meanCqi /= snr.size();
    if(meanCqi < 2)
        meanCqi = 2;

    return meanCqi;
}

std::list<int>::const_iterator BackgroundTrafficManager::getBackloggedUesBegin(Direction dir)
{
    return backloggedBgUes_[dir].begin();
}

std::list<int>::const_iterator BackgroundTrafficManager::getBackloggedUesEnd(Direction dir)
{
    return backloggedBgUes_[dir].end();
}

unsigned int BackgroundTrafficManager::getBackloggedUeBuffer(MacNodeId bgUeId, Direction dir)
{
    int index = bgUeId - BGUE_MIN_ID;
    return bgUe_.at(index)->getBufferLength(dir);
}

unsigned int BackgroundTrafficManager::getBackloggedUeBytesPerBlock(MacNodeId bgUeId, Direction dir)
{
    int index = bgUeId - BGUE_MIN_ID;
    Cqi cqi = bgUe_.at(index)->getCqi(dir);

    // get bytes per block based on CQI
    return (mac_->getAmc()->computeBitsPerRbBackground(cqi, dir, carrierFrequency_) / 8);
}
unsigned int BackgroundTrafficManager::consumeBackloggedUeBytes(MacNodeId bgUeId, unsigned int bytes, Direction dir)
{
    int index = bgUeId - BGUE_MIN_ID;
    int newBuffLen = bgUe_.at(index)->consumeBytes(bytes, dir);

    if (newBuffLen == 0)  // bg UE is no longer active
        backloggedBgUes_[dir].remove(index);

    return newBuffLen;
}

