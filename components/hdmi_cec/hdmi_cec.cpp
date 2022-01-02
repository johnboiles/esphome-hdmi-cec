#include "hdmi_cec.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hdmi_cec {

static const char *const TAG = "hdmi_cec";

void messageToDebugString(char *message, unsigned char *buffer, int count) {
    for (int i = 0; i < count; i++) {
      sprintf(&(message[i*3]), "%02X", buffer[i]);
      if (i < count - 1) {
        sprintf(&(message[i*3+2]), ":");
      }
    }
}

HdmiCec::HdmiCec() {
  ESP_LOGI(TAG, "HdmiCec");
}

void HdmiCec::setup() {
  this->high_freq_.start();

  ESP_LOGCONFIG(TAG, "Setting up HDMI-CEC...");
  // TODO: Can i set this at compile time so I don't have to use heap space?
  this->ceclient_ = new CEClient(0x00, 4, 5);
  this->ceclient_->begin(CEC_LogicalDevice::CDT_AUDIO_SYSTEM);
  this->ceclient_->setPromiscuous(true);

  std::function<void(int, int, unsigned char*, int)> messageReceived = [this](int source, int destination, unsigned char* buffer, int count) {
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
  this->ceclient_->onReceiveCallback(messageReceived);
}

void HdmiCec::dump_config() {
  ESP_LOGCONFIG(TAG, "HDMI-CEC:");
  ESP_LOGCONFIG(TAG, "  address: %d", this->address_);
  // LOG_PIN("  Output Pin: ", this->gate_pin_);
}

void HdmiCec::send_data(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data) {
    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(data.data());
    auto charBuffer = const_cast<unsigned char *>(buffer);

    char debugMessage[HDMI_CEC_MAX_DATA_LENGTH*3];
    messageToDebugString(debugMessage, charBuffer, data.size());
    ESP_LOGD(TAG, "TX: (%d->%d) %02X:%s", source, destination, ((source&0x0f)<<4)|(destination&0x0f), debugMessage);

    this->ceclient_->TransmitFrame(destination, charBuffer, data.size());
}

void HdmiCec::add_trigger(HdmiCecTrigger *trigger) {
  this->triggers_.push_back(trigger);
};

int counter = 0;
int timer = 0;

void HdmiCec::loop() {
  this->ceclient_->run();

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
