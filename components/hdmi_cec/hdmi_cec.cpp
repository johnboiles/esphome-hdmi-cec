#include "hdmi_cec.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hdmi_cec {

static const char *const TAG = "hdmi_cec";

void message_to_debug_string(char *message, const unsigned char *buffer, int count) {
  for (int i = 0; i < count; i++) {
    sprintf(&(message[i * 3]), "%02X", buffer[i]);
    if (i < count - 1) {
      sprintf(&(message[i * 3 + 2]), ":");
    }
  }
}

bool MyCecDevice::LineState() {
  int state = this->pin_->digital_read();
  return state != LOW;
}

void MyCecDevice::SetLineState(bool state) {
  if (state) {
    this->pin_->pin_mode(gpio::FLAG_INPUT);
  } else {
    this->pin_->digital_write(false);
    this->pin_->pin_mode(gpio::FLAG_OUTPUT);
  }
}

void MyCecDevice::OnReady(int logical_address) { this->on_ready_(logical_address); }

void MyCecDevice::OnReceiveComplete(unsigned char *buffer, int count, bool ack) {
  // No command received?
  if (count < 2)
    return;

  auto source = (buffer[0] & 0xF0) >> 4;
  auto destination = (buffer[0] & 0x0F);
  this->on_receive_(source, destination, &(buffer[1]), count - 1);
}

void MyCecDevice::OnTransmitComplete(unsigned char *buffer, int count, bool ack) {}

// Used so that `pin_interrupt` doesn't fire when we're toggling the line
// TODO: Move this to an instance variable
volatile boolean disable_line_interrupts = false;

void IRAM_ATTR HOT HdmiCec::pin_interrupt(HdmiCec *arg) {
  if (disable_line_interrupts)
    return;
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

  this->ceclient_.on_receive_ = [this](int source, int destination, unsigned char *buffer, int count) {
    // If we're not in promiscuous mode and the message isn't for us, ignore it.
    if (!this->promiscuous_mode_ && destination != this->address_ && destination != 0xF) {
      return;
    }

    char debugMessage[HDMI_CEC_MAX_DATA_LENGTH * 3];
    message_to_debug_string(debugMessage, buffer, count);
    ESP_LOGD(TAG, "RX: (%d->%d) %02X:%s", source, destination, ((source & 0x0f) << 4) | (destination & 0x0f),
             debugMessage);

    // Handling the physical address response in code instead of yaml since I think it always
    // needs to happen for other devices to be able to talk to this device.
    if (buffer[0] == 0x83 && destination == address_) {
      // Report physical address
      unsigned char buf[4] = {0x84, (unsigned char) (physical_address_ >> 8),
                              (unsigned char) (physical_address_ & 0xff), address_};
      this->send_data_internal_(this->address_, 0xF, buf, 4);
    }

    uint8_t opcode = buffer[0];
    for (auto trigger : this->triggers_) {
      if ((!trigger->opcode_.has_value() || (*trigger->opcode_ == opcode)) &&
          (!trigger->source_.has_value() || (*trigger->source_ == source)) &&
          (!trigger->destination_.has_value() || (*trigger->destination_ == destination)) &&
          (!trigger->data_.has_value() ||
           (count == trigger->data_->size() && std::equal(trigger->data_->begin(), trigger->data_->end(), buffer)))) {
        auto dataVec = std::vector<uint8_t>(buffer, buffer + count);
        trigger->trigger(source, destination, dataVec);
      }
    }
  };
  this->ceclient_.on_ready_ = [this](int logical_address) {
    // This is called after the logical address has been allocated
    ESP_LOGD(TAG, "Device ready, Logical address assigned: %d", logical_address);
    this->address_ = logical_address;
    // Report physical address
    unsigned char buf[4] = {0x84, (unsigned char) (physical_address_ >> 8), (unsigned char) (physical_address_ & 0xff),
                            address_};
    this->send_data_internal_(this->address_, 0xF, buf, 4);
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

  this->send_data_internal_(source, destination, charBuffer, data.size());
}

void HdmiCec::send_data_internal_(uint8_t source, uint8_t destination, unsigned char *buffer, int count) {
  char debugMessage[HDMI_CEC_MAX_DATA_LENGTH * 3];
  message_to_debug_string(debugMessage, buffer, count);
  ESP_LOGD(TAG, "TX: (%d->%d) %02X:%s", source, destination, ((source & 0x0f) << 4) | (destination & 0x0f),
           debugMessage);

  this->ceclient_.TransmitFrame(destination, buffer, count);
}

void HdmiCec::add_trigger(HdmiCecTrigger *trigger) { this->triggers_.push_back(trigger); };

// TODO: Move this to static or remove
int counter = 0;
int timer = 0;

void HdmiCec::loop() {
  disable_line_interrupts = true;
  this->ceclient_.Run();
  disable_line_interrupts = false;

  // The current implementation of CEC is inefficient and relies on polling to
  // identify signal changes at just the right time. Experimentally it needs to
  // run faster than every ~0.04ms to be reliable. This can be solved by creating
  // an interrupt-driven CEC driver.
  if (millis() - timer > 10000) {
    ESP_LOGD(TAG, "Ran %d times in 10000ms (every %fms)", counter, 10000.0f / (float) counter);
    counter = 0;
    timer = millis();
  }
  counter++;
}

}  // namespace hdmi_cec
}  // namespace esphome
