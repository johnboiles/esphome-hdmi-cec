#include "hdmi_cec.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace hdmi_cec {

static const char *const TAG = "hdmi_cec";

void transmitComplete(bool complete) {
    ESP_LOGI(TAG, "Transmit complete %d", complete);
}

void messageReceived(int source, int dest, unsigned char* buffer, int count) {
  ESP_LOGI(TAG, "RX: %02d -> %02d: %02X", source, dest, ((source&0x0f)<<4)|(dest&0x0f));
  for (int i = 0; i < count; i++) {
    ESP_LOGI(TAG, ":%02X", buffer[i]);
  }
}

HdmiCec::HdmiCec() {
  ESP_LOGI(TAG, "HdmiCec");
}

void HdmiCec::setup() {
  this->high_freq_.start();

  ESP_LOGCONFIG(TAG, "Setting up HDMI-CEC...");
  // create a CEC client
  // TODO: Can i set this at compile time so I don't have to use heap space?
  this->ceclient_ = new CEClient(0x00, 4, 5);
  this->ceclient_->begin(CEC_LogicalDevice::CDT_AUDIO_SYSTEM);
  this->ceclient_->setPromiscuous(true);
  // this->ceclient_->setMonitorMode(true);
  this->ceclient_->onTransmitCompleteCallback(transmitComplete);
  this->ceclient_->onReceiveCallback(messageReceived);

  // this->pin_->setup();
  // this->isr_pin_ = pin_->to_isr();
  // this->pin_->attach_interrupt(PulseMeterSensor::gpio_intr, this, gpio::INTERRUPT_ANY_EDGE);

  ESP_LOGI(TAG, "HDMI complete");
}

void HdmiCec::dump_config() {
  ESP_LOGI(TAG, "dump_config");
}

void HdmiCec::send_data(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data) {
    // ESP_LOGD(TAG, "TX: %02d -> %02d: %02X", source, destination, ((source&0x0f)<<4)|(destination&0x0f));
    // unsigned char buffer[16];
    // for (int i = 0; i < data.size(); i++) {
    //   ESP_LOGD(TAG, ":%02X", data[i]);
    //   buffer[i] = data[i];
    // }

    const uint8_t *buffer = reinterpret_cast<const uint8_t *>(data.data());
    this->ceclient_->TransmitFrame(destination, const_cast<unsigned char *>(buffer), data.size());
    // this->ceclient_->TransmitFrame(destination, buffer, data.size());
}

void HdmiCec::add_trigger(HdmiCecTrigger *trigger) {
  this->triggers_.push_back(trigger);
};

int counter = 0;
int timerrr = 0;

void HdmiCec::loop() {
  this->ceclient_->run();

  // We're only running every 16ms which is prob too slow. Sometimes at startup it runs
  // 4904 times per second or every .2ms and thats why it works sometimes
  // My sample code runs 274238 tims in 5000ms or every 0.01823234ms
  // So in short I've got to convert this to using an ISR

  // high frequency loop gets me to Ran 17269 tims in 5000ms or about 0.28 ms which can transmit fine
  // but is still not receiving well hmm
  // with high freq and disabling wifi i can get to 106647 tims in 5000ms or 0.04716981 ms
  if (millis() - timerrr > 5000) {
    ESP_LOGI(TAG, "Ran %d tims in 5000ms", counter);
    counter = 0;
    timerrr = millis();
  }
  counter++;
}

}  // namespace hdmi_cec
}  // namespace esphome
