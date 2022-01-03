#include "CEC_Device.h"

CEC_Device::CEC_Device() :
	_monitorMode(true),
	_promiscuous(false),
	_logicalAddress(-1),
	_state(CEC_IDLE),
	_receiveBufferBits(0),
	_transmitBufferBytes(0),
	_amLastTransmittor(false),
	_lineSetTime(0),
	_bitStartTime(0),
	_waitTime(0)
{
}

void CEC_Device::Initialize(int physicalAddress, CEC_DEVICE_TYPE type, bool promiscuous, bool monitorMode)
{
	static const char valid_LogicalAddressesTV[3]    = {CLA_TV, CLA_FREE_USE, CLA_UNREGISTERED};
	static const char valid_LogicalAddressesRec[4]   = {CLA_RECORDING_DEVICE_1, CLA_RECORDING_DEVICE_2,
	                                                    CLA_RECORDING_DEVICE_3, CLA_UNREGISTERED};
	static const char valid_LogicalAddressesPlay[4]  = {CLA_PLAYBACK_DEVICE_1, CLA_PLAYBACK_DEVICE_2,
	                                                    CLA_PLAYBACK_DEVICE_3, CLA_UNREGISTERED};
	static const char valid_LogicalAddressesTuner[5] = {CLA_TUNER_1, CLA_TUNER_2, CLA_TUNER_3,
	                                                    CLA_TUNER_4, CLA_UNREGISTERED};
	static const char valid_LogicalAddressesAudio[2] = {CLA_AUDIO_SYSTEM, CLA_UNREGISTERED};

	switch(type) {
	case CDT_TV:               _validLogicalAddresses = valid_LogicalAddressesTV;    break;
	case CDT_RECORDING_DEVICE: _validLogicalAddresses = valid_LogicalAddressesRec;   break;
	case CDT_PLAYBACK_DEVICE:  _validLogicalAddresses = valid_LogicalAddressesPlay;  break;
	case CDT_TUNER:            _validLogicalAddresses = valid_LogicalAddressesTuner; break;
	case CDT_AUDIO_SYSTEM:     _validLogicalAddresses = valid_LogicalAddressesAudio; break;
	default:                   _validLogicalAddresses = NULL;
	}

	_promiscuous = promiscuous;
	_monitorMode = monitorMode;
	_physicalAddress = physicalAddress & 0xffff;
	_logicalAddress = -1;

	// <Polling Message> to allocate a logical address when physical address is valid
	if (_validLogicalAddresses && _physicalAddress != 0xffff)
		Transmit(*_validLogicalAddresses, *_validLogicalAddresses, NULL, 0);
}

///
/// CEC_Device::Run implements our main state machine
/// which includes all reading and writing of state including
/// acknowledgements and arbitration
///

void CEC_Device::Run()
{
	unsigned long time = micros();
	unsigned long difftime = time - _lineSetTime;
	if (difftime < (_lastLineState ? CEC_MAX_RISE_TIME : CEC_MAX_FALL_TIME))
		// give enough time for the line to settle before sampling it
		return;

	bool currentLineState = LineState();
	difftime = time - _bitStartTime;
	if (currentLineState == _lastLineState && _waitTime > difftime)
		// No line transition and wait time not elapsed; nothing to do
		return;

	if (currentLineState != _lastLineState &&
	    _state >= CEC_XMIT_WAIT && _state != CEC_XMIT_ACK_TEST && _state != CEC_XMIT_ACK_WAIT)
		// We are in a transmit state and someone else is mucking with the line
		// Try to receive and wait for the line to clear before (re)transmit
		// However, it is OK for a follower to ACK if we are in an ACK state
		_state = CEC_IDLE;

	bool bit;
	_waitTime = 0;
	switch (_state) {
	case CEC_IDLE:
		// If a high to low transition occurs, this must be the beginning of a start bit
		if (!currentLineState) {
			_receiveBufferBits = 0;
			_bitStartTime = time;
			_ack = true;
			_follower = false;
			_broadcast = false;
			_amLastTransmittor = false;
			_waitTime = STARTBIT_TIMEOUT;
			_state = CEC_RCV_STARTBIT1;
		} else if (_transmitBufferBytes)
			// Transmit pending
			if (_xmitretry > CEC_MAX_RETRANSMIT)
				// No more
				_transmitBufferBytes = 0;
			else {
				// We need to wait a certain amount of time before we can transmit
				_waitTime = ((_xmitretry) ?  3 * BIT_TIME :
				             (_amLastTransmittor) ? 7 * BIT_TIME :
				             5 * BIT_TIME);
				_state = CEC_XMIT_WAIT;
			}
		// Nothing to do until we have a need to transmit
		// or we detect the falling edge of the start bit
		break;

	case CEC_RCV_STARTBIT1:
		// We received the rising edge of the start bit
		if (difftime >= (STARTBIT_TIME_LOW - BIT_TIME_LOW_MARGIN) &&
		    difftime <= (STARTBIT_TIME_LOW + BIT_TIME_LOW_MARGIN)) {
			// We now need to wait for the next falling edge
			_waitTime = STARTBIT_TIMEOUT;
			_state = CEC_RCV_STARTBIT2;
			break;
		}
		// Illegal state.  Go back to CEC_IDLE to wait for a valid start bit or start pending transmit
		_state = CEC_IDLE;
		break;

	case CEC_RCV_STARTBIT2:
		// This should be the falling edge after the start bit
		if (difftime >= (STARTBIT_TIME - BIT_TIME_MARGIN) &&
		    difftime <= (STARTBIT_TIME + BIT_TIME_MARGIN)) {
			// We've fully received the start bit.  Begin receiving a data bit
			_bitStartTime = time;
			_waitTime = BIT_TIMEOUT;
			_state = CEC_RCV_DATABIT1;
			break;
		}
		// Illegal state.  Go back to CEC_IDLE to wait for a valid start bit or start pending transmit
		_state = CEC_IDLE;
		break;

	case CEC_RCV_DATABIT1:
	case CEC_RCV_EOM1:
	case CEC_RCV_ACK1:
		// We've received the rising edge of the data/eom/ack bit
		if (difftime >= (BIT_TIME_LOW_1 - BIT_TIME_LOW_MARGIN) &&
		    difftime <= (BIT_TIME_LOW_1 + BIT_TIME_LOW_MARGIN))
			bit = true;
		else if (difftime >= (BIT_TIME_LOW_0 - BIT_TIME_LOW_MARGIN) &&
		         difftime <= (BIT_TIME_LOW_0 + BIT_TIME_LOW_MARGIN))
			bit = false;
		else {
			// Illegal state.  Send NAK later.
			bit = true;
			_ack = false;
		}
		if (_state == CEC_RCV_EOM1) {
			_eom = bit;
		}
		else if (_state == CEC_RCV_ACK1) {
			_ack = (bit == _broadcast);
			if (_eom || !_ack) {
				// We're not going to receive anything more from the initiator.
				// Go back to the IDLE state and wait for another start bit or start pending transmit.
				OnReceiveComplete(_receiveBuffer, _receiveBufferBits >> 3, _ack);
				_state = CEC_IDLE;
				break;
			}
		} else {
			// Save the received bit
			unsigned int idx = _receiveBufferBits >> 3;
			if (idx < sizeof(_receiveBuffer)) {
				_receiveBuffer[idx] = (_receiveBuffer[idx] << 1) | bit;
				_receiveBufferBits++;
			}
		}
		_waitTime = BIT_TIMEOUT;
		_state = (CEC_STATE)(_state + 1);
		break;

	case CEC_RCV_DATABIT2:
	case CEC_RCV_EOM2:
	case CEC_RCV_ACK2:
		if (difftime > (BIT_TIME + BIT_TIME_MARGIN)) {
			// Illegal state.  Timeout?
			_state = CEC_IDLE;
			break;
		}
		// We've received the falling edge after the data/eom/ack bit
		_bitStartTime = time;
		if (difftime >= (BIT_TIME - BIT_TIME_MARGIN)) {
			if (_state == CEC_RCV_EOM2) {
				_waitTime = BIT_TIMEOUT;
				_state = CEC_RCV_ACK1;

				// Check to see if the frame is addressed to us
				// or if we are in promiscuous mode (in which case we'll receive everything)
				int address = _receiveBuffer[0] & 0x0f;
				if (address == 0x0f)
					_broadcast = true;
				else if (address == _logicalAddress)
					_follower = true;

				// Go low for ack/nak
				if ((_follower && _ack) || (_broadcast && !_ack)) {
					if (!_monitorMode) {
						SetLineState(0);
						currentLineState = 0;
						_lineSetTime = time;
					}
					_waitTime = BIT_TIME_LOW_0;
					_state = CEC_RCV_ACK_SENT;
				} else if (!_ack || (!_promiscuous && !_broadcast)) {
					// It's broken or not addressed to us.
					// Go back to CEC_IDLE to wait for a valid start bit or start pending transmit
					_state = CEC_IDLE;
				}
				break;
			}
			// Receive another bit
			_waitTime = BIT_TIMEOUT;
			_state = (_state == CEC_RCV_DATABIT2 &&
			          (_receiveBufferBits & 7) == 0) ? CEC_RCV_EOM1 : CEC_RCV_DATABIT1;
			break;
		}
		// Line error.
		if (_monitorMode) {
			_state = CEC_IDLE;
			break;
		}
		SetLineState(0);
		currentLineState = 0;
		_lineSetTime = time;
		_waitTime = BIT_TIME_ERR;
		_state = CEC_RCV_LINEERROR;
		break;

	case CEC_RCV_ACK_SENT:
		// We're done holding the line low...  release it
		SetLineState(1);
		currentLineState = 1;
		_lineSetTime = time;
		if (_eom || !_ack) {
			// We're not going to receive anything more from the initiator (EOM has been received)
			// or we've sent the NAK for the most recent bit. Therefore this message is all done.
			// Go back to CEC_IDLE to wait for a valid start bit or start pending transmit
			OnReceiveComplete(_receiveBuffer, _receiveBufferBits >> 3, _ack);
			_state = CEC_IDLE;
			break;
		}
		// We need to wait for the falling edge of the ACK to finish processing this ack
		_waitTime = BIT_TIMEOUT;
		_state = CEC_RCV_ACK2;
		break;

	case CEC_RCV_LINEERROR:
		SetLineState(1);
		currentLineState = 1;
		_lineSetTime = time;
		_state = CEC_IDLE;
		break;

	case CEC_XMIT_WAIT:
		// We waited long enough, begin start bit
		SetLineState(0);
		currentLineState = 0;
		_lineSetTime = time;
		_bitStartTime = time;
		_transmitBufferBitIdx = 0;
		_xmitretry++;
		_amLastTransmittor = true;
		_broadcast = (_transmitBuffer[0] & 0x0f) == 0x0f;
		_waitTime = STARTBIT_TIME_LOW;
		_state = CEC_XMIT_STARTBIT1;
		break;

	case CEC_XMIT_STARTBIT1:
	case CEC_XMIT_DATABIT1:
	case CEC_XMIT_EOM1:
		// We finished the first half of the bit, send the rising edge
		SetLineState(1);
		currentLineState = 1;
		_lineSetTime = time;
		_waitTime = (_state == CEC_XMIT_STARTBIT1) ? STARTBIT_TIME : BIT_TIME;
		_state = (CEC_STATE)(_state + 1);
		break;

	case CEC_XMIT_STARTBIT2:
	case CEC_XMIT_DATABIT2:
	case CEC_XMIT_EOM2:
	case CEC_XMIT_ACK2:
		// We finished the second half of the previous bit, send the falling edge of the new bit
		SetLineState(0);
		currentLineState = 0;
		_lineSetTime = time;
		_bitStartTime = time;
		if (_state == CEC_XMIT_DATABIT2 && (_transmitBufferBitIdx & 7) == 0) {
			_state = CEC_XMIT_EOM1;
			// Get EOM bit: transmit buffer empty?
			bit = _eom = (_transmitBufferBytes == (_transmitBufferBitIdx >> 3));
		} else if (_state == CEC_XMIT_EOM2) {
			_state = CEC_XMIT_ACK1;
			bit = true; // We transmit a '1'
		} else {
			_state = CEC_XMIT_DATABIT1;
			// Pull bit from transmit buffer
			unsigned char b = _transmitBuffer[_transmitBufferBitIdx >> 3] << (_transmitBufferBitIdx++ & 7);
			bit = b >> 7;
		}
		_waitTime = bit ? BIT_TIME_LOW_1 : BIT_TIME_LOW_0;
		break;

	case CEC_XMIT_ACK1:
		// We finished the first half of the ack bit, release the line
		SetLineState(1); // Maybe follower pulls low for ACK
		currentLineState = 1;
		_lineSetTime = time;
		_waitTime = BIT_TIME_SAMPLE;
		_state = CEC_XMIT_ACK_TEST;
		break;

	case CEC_XMIT_ACK_TEST:
		if ((currentLineState != 0) != _broadcast) {
			// Not being acknowledged
			OnTransmitComplete(_transmitBuffer, _transmitBufferBitIdx >> 3, false);

			// Normally we retransmit.  But this is NOT the case for <Polling Message> as its
			// function is basically to 'ping' a logical address in which case we just want
			// acknowledgement that it has succeeded or failed
			if (_transmitBufferBytes == 1)
				_transmitBufferBytes = 0;

			if (_logicalAddress < 0) {
				// Claim this as our logical address
				_logicalAddress = *_validLogicalAddresses;
				OnReady(_logicalAddress);
			}

			_state = CEC_IDLE;
			break;
		}
		if (_eom) {
			// Nothing left to transmit, go back to idle
			_transmitBufferBytes = 0;
			OnTransmitComplete(_transmitBuffer, _transmitBufferBitIdx >> 3, true);

			if (_logicalAddress < 0) {
				// Someone is there, try to allocate the next possible logical address
				if (*++_validLogicalAddresses != CLA_UNREGISTERED) {
					Transmit(*_validLogicalAddresses, *_validLogicalAddresses, NULL, 0);
				} else {
					// No other logical address, use CLA_UNREGISTERED
					_logicalAddress = CLA_UNREGISTERED;
					OnReady(_logicalAddress);
				}
			}

			_state = CEC_IDLE;
			break;
		}
		// We have more to transmit, so do so...
		_waitTime = BIT_TIME;
		_state = CEC_XMIT_ACK_WAIT;  // wait for line going high
		if (currentLineState != 0)   // already high, no ACK from follower
			_state = CEC_XMIT_ACK2;
		break;
	case CEC_XMIT_ACK_WAIT:
		// We received the rising edge of ack from follower
		_waitTime = BIT_TIME;
		_state = CEC_XMIT_ACK2;
		break;
	}
	_lastLineState = currentLineState;
}

bool CEC_Device::Transmit(int sourceAddress, int targetAddress, const unsigned char* buffer, unsigned int count)
{
	if (_monitorMode)
		return false; // we must not transmit in monitor mode
	if (_transmitBufferBytes != 0)
		return false; // pending transmit packet
	if (count >= sizeof(_transmitBuffer))
		return false; // packet too big

	_transmitBuffer[0] = (sourceAddress << 4) | (targetAddress & 0xf);
	for (int i = 0; i < count; i++)
		_transmitBuffer[i+1] = buffer[i];
	_transmitBufferBytes = count + 1;
	_xmitretry = 0;
	return true;
}

bool CEC_Device::TransmitFrame(int targetAddress, const unsigned char* buffer, int count)
{
	if (_logicalAddress < 0)
		return false;

	return Transmit(_logicalAddress, targetAddress, buffer, count);
}
