#include "CEClient.h"

// create a new instance of CEClient
CEClient::CEClient(int physicalAddress, int inputPin, int outputPin) : 
    CEC_Device(physicalAddress, inputPin) {

    _ready = false;
}

// init CEClient 
void CEClient::begin(CEC_DEVICE_TYPE type) {

    CEC_Device::Initialize(type);
}

// return the ready state
bool CEClient::isReady() {

    return _ready;
}

// write a packet on the bus
bool CEClient::write(int targetAddress, unsigned char* buffer, int count) {

    return CEC_Device::TransmitFrame(targetAddress, buffer, count);
}

// return the logical address
int CEClient::getLogicalAddress() {

    return _logicalAddress;
}

// enable-disable promiscuous mode
void CEClient::setPromiscuous(bool promiscuous) {
    Promiscuous = promiscuous;
}

// enable-disable monitor mode
void CEClient::setMonitorMode(bool monitorMode) {
    DbgPrint("setMonitorMode\n");
    MonitorMode = monitorMode;
}

// set callback function when a transmit is complete
void CEClient::onTransmitCompleteCallback(OnTransmitCompleteCallbackFunction callback) {

    _onTransmitCompleteCallback = callback;
}

// set callback function when a new packet is received
void CEClient::onReceiveCallback(OnReceiveCallbackFunction callback) {

    _onReceiveCallback = callback;
}

// run, to be executed in the loop for the FSM
void CEClient::run() {

    CEC_Device::Run();
}


// ----- PRIVATE METHODS -----

// OnTransmitComplete redefinition, if a callback function is available, call it
void CEClient::OnTransmitComplete(bool success) {

    if(_onTransmitCompleteCallback) 
        _onTransmitCompleteCallback(success);
    
    CEC_Device::OnTransmitComplete(success);
}

// OnReceive redefinition, if a callback function is available, call it
// if not, call the parent function
void CEClient::OnReceive(int source, int dest, unsigned char* buffer, int count) {

    if(_onReceiveCallback)
        _onReceiveCallback(source, dest, buffer, count);        
    else
        CEC_Device::OnReceive(source, dest, buffer, count);
    
    if (!MonitorMode && dest == _logicalAddress && count == 1 && buffer[0] == 0x83) {
      unsigned char buffer[4];
      buffer[0] = 0x84;
    //   buffer[1] = _physicalAddress >> 8;
    //   buffer[2] = _physicalAddress;
    //   buffer[3] = _deviceType;
      buffer[1] = 0x40;
      buffer[2] = 0x00;
      buffer[3] = _deviceType;
      TransmitFrame(0xF, buffer, 4);
    }  //5F:84:40:00:05
}

// OnReady redefinition, to save the current status
void CEClient::OnReady() {

    _ready = true;
}
