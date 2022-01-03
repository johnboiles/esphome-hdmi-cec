#include "hdmi_cec.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hdmi_cec {

static const char *const TAG = "hdmi_cec";
// TODO: Make this configurable
#define CEC_PHYSICAL_ADDRESS 0x2000

void messageToDebugString(char *message, unsigned char *buffer, int count) {
    for (int i = 0; i < count; i++) {
      sprintf(&(message[i*3]), "%02X", buffer[i]);
      if (i < count - 1) {
        sprintf(&(message[i*3+2]), ":");
      }
    }
}

bool MyCEC_Device::LineState()
{
	int state = digitalRead(pin_);
	return state != LOW;
}

void MyCEC_Device::SetLineState(bool state)
{
	if (state) {
		pinMode(pin_, INPUT_PULLUP);
	} else {
		digitalWrite(pin_, LOW);
		pinMode(pin_, OUTPUT);
	}
}

void MyCEC_Device::OnReady(int logicalAddress)
{
  // This is called after the logical address has been allocated

	unsigned char buf[4] = {0x84, CEC_PHYSICAL_ADDRESS >> 8, CEC_PHYSICAL_ADDRESS & 0xff, address_};

	ESP_LOGD(TAG, "Device ready, Logical address assigned: %d", logicalAddress);

	TransmitFrame(0xf, buf, 4); // <Report Physical Address>
}

void MyCEC_Device::OnReceiveComplete(unsigned char* buffer, int count, bool ack)
{
	// No command received?
	if (count < 2)
		return;

  auto source = (buffer[0] & 0xF0) >> 4;
  auto destination = (buffer[0] & 0x0F);

	switch (buffer[1]) {
    // Handling the physical address in code instead of yaml since it's pretty boilerplate
    case 0x83: {
      unsigned char buf[4] = {0x84, CEC_PHYSICAL_ADDRESS >> 8, CEC_PHYSICAL_ADDRESS & 0xff, address_};
      TransmitFrame(0xf, buf, 4); // <Report Physical Address>
      char debugMessage[HDMI_CEC_MAX_DATA_LENGTH*3];
      messageToDebugString(debugMessage, buf, 4);
      ESP_LOGD(TAG, "TX: (%d->%d) %02X:%s (physical address)", source, destination, ((source&0x0f)<<4)|(destination&0x0f), debugMessage);
      break;
    }
  }

  this->on_receive_(source, destination, &(buffer[1]), count - 1);
}

void MyCEC_Device::OnTransmitComplete(unsigned char* buffer, int count, bool ack) {
}

// Used so that `pin_interrupt` doesn't fire when we're toggling the line
volatile boolean disableLineInterrupts = false;

void IRAM_ATTR HdmiCec::pin_interrupt(HdmiCec *arg) {
  if (disableLineInterrupts) return;
	arg->ceclient_.Run();
}

void HdmiCec::setup() {
  this->high_freq_.start();

  ESP_LOGCONFIG(TAG, "Setting up HDMI-CEC...");
  this->ceclient_.set_pin(this->pin_);
	this->ceclient_.Initialize(0x2000, CEC_Device::CDT_AUDIO_SYSTEM, true);

  // This isn't quite enough to allow us to get rid of the HighFrequencyLoopRequester.
  // There's probably something that needs to wait a certain amount of time after
  // an interrupt.
  this->pin_->attach_interrupt(HdmiCec::pin_interrupt, this, gpio::INTERRUPT_ANY_EDGE);

  this->ceclient_.on_receive_ = [this](int source, int destination, unsigned char* buffer, int count) {
    char debugMessage[HDMI_CEC_MAX_DATA_LENGTH*3];
    messageToDebugString(debugMessage, buffer, count);
    ESP_LOGD(TAG, "RX: (%d->%d) %02X:%s", source, destination, ((source&0x0f)<<4)|(destination&0x0f), debugMessage);

    uint8_t opcode = buffer[0];
    for (auto trigger : this->triggers_) {
      if ((!trigger->opcode_.has_value() || (*trigger->opcode_ == opcode)) &&
         (!trigger->source_.has_value() || (*trigger->source_ == source)) &&
         (!trigger->destination_.has_value() || (*trigger->destination_ == destination)) &&
         (!trigger->data_.has_value() || (count == trigger->data_->size() && std::equal(trigger->data_->begin(), trigger->data_->end(), buffer)))) {
        auto dataVec = std::vector<uint8_t>(buffer, buffer+count);
        trigger->trigger(source, destination, dataVec);
      }
    }
  };
}

void HdmiCec::dump_config() {
  ESP_LOGCONFIG(TAG, "HDMI-CEC:");
  ESP_LOGCONFIG(TAG, "  address: %d", this->address_);
  LOG_PIN("  Pin: ", this->pin_);
}

void HdmiCec::send_data(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data) {
    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(data.data());
    auto charBuffer = const_cast<unsigned char *>(buffer);

    char debugMessage[HDMI_CEC_MAX_DATA_LENGTH*3];
    messageToDebugString(debugMessage, charBuffer, data.size());
    ESP_LOGD(TAG, "TX: (%d->%d) %02X:%s", source, destination, ((source&0x0f)<<4)|(destination&0x0f), debugMessage);

    this->ceclient_.TransmitFrame(destination, charBuffer, data.size());
}

void HdmiCec::add_trigger(HdmiCecTrigger *trigger) {
  this->triggers_.push_back(trigger);
};

int counter = 0;
int timer = 0;

void HdmiCec::loop() {
  disableLineInterrupts = true;
  this->ceclient_.Run();
  disableLineInterrupts = false;

  // The current implementation of CEC is inefficient and relies on polling to
  // identify signal changes at just the right time. Experimentally it needs to 
  // run faster than every ~0.04ms to be reliable. This can be solved by creating
  // an interrupt-driven CEC driver.
  if (millis() - timer > 10000) {
    ESP_LOGD(TAG, "Ran %d times in 10000ms (every %fms)", counter, 10000.0f / (float)counter);
    counter = 0;
    timer = millis();
  }
  counter++;
}

}  // namespace hdmi_cec
}  // namespace esphome
