#ifndef CEC_DEVICE_H__
#define CEC_DEVICE_H__

#include "CEC.h"

class CEC_Device : public CEC_LogicalDevice
{
public:
  CEC_Device(int physicalAddress, int in_line);
  
  void Initialize(CEC_DEVICE_TYPE type);
  virtual void Run();
  virtual void SignalIRQ();
    bool _lastLineState2;
  virtual bool LineState();
  virtual bool IsISRTriggered();

protected:
  virtual void SetLineState(bool);
  virtual bool IsISRTriggered2() { return _isrTriggered; }

  virtual void OnReady();
  virtual void OnReceive(int source, int dest, unsigned char* buffer, int count);
  
private:
  bool _isrTriggered;
  int  _in_line;
};

#endif // CEC_DEVICE_H__
