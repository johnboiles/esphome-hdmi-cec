#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"
#include "esphome/core/log.h"

#include "CEClient.h"

namespace esphome {
namespace hdmi_cec {

enum Error : uint8_t {
  ERROR_OK = 0,
  ERROR_FAIL = 1,
  ERROR_ALLTXBUSY = 2,
  ERROR_FAILINIT = 3,
  ERROR_FAILTX = 4,
  ERROR_NOMSG = 5
};


class HdmiCecTrigger;
template<typename... Ts> class HdmiCecSendAction;

static const uint8_t HDMI_CEC_MAX_DATA_LENGTH = 16;

/*
Can Frame describes a normative CAN Frame
The RTR = Remote Transmission Request is implemented in every CAN controller but rarely used
So currently the flag is passed to and from the hardware but currently ignored to the user application.
*/
// struct CanFrame {
//   bool use_extended_id = false;
//   bool remote_transmission_request = false;
//   uint32_t can_id;              /* 29 or 11 bit CAN_ID  */
//   uint8_t can_data_length_code; /* frame payload length in byte (0 .. HDMI_CEC_MAX_DATA_LENGTH) */
//   uint8_t data[HDMI_CEC_MAX_DATA_LENGTH] __attribute__((aligned(8)));
// };



class HdmiCec : public Component {
 public:
  HdmiCec();
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void loop() override;

  void send_data(uint8_t source, uint8_t destination, const std::vector<uint8_t> &data);
  void set_source(uint8_t source) { this->source_ = source; }

  void add_trigger(HdmiCecTrigger *trigger);

 protected:
  template<typename... Ts> friend class HdmiCecSendAction;
  std::vector<HdmiCecTrigger *> triggers_{};
  uint32_t source_;
  CEClient *ceclient_;
  HighFrequencyLoopRequester high_freq_;

  // virtual bool setup_internal();
  // virtual Error send_message(struct CanFrame *frame);
  // virtual Error read_message(struct CanFrame *frame);
};

template<typename... Ts> class HdmiCecSendAction : public Action<Ts...>, public Parented<HdmiCec> {
 public:
  void set_data_template(const std::function<std::vector<uint8_t>(Ts...)> func) {
    this->data_func_ = func;
    this->static_ = false;
  }
  void set_data_static(const std::vector<uint8_t> &data) {
    this->data_static_ = data;
    this->static_ = true;
  }
  void set_source(uint8_t source) { this->source_ = source; }
  void set_destination(uint8_t destination) { this->destination_ = destination; }

  void play(Ts... x) override {
    auto source = this->source_.has_value() ? *this->source_ : this->parent_->source_;
    ESP_LOGD("SendAction", "play %d", this->parent_);
    if (this->static_) {
    ESP_LOGD("SendAction", "static %d", this->parent_);
      this->parent_->send_data(source, this->destination_, this->data_static_);
    } else {
      auto val = this->data_func_(x...);
      this->parent_->send_data(source, this->destination_, val);
    }
  }

 protected:

  // InternalGPIOPin *input_pin_ = nullptr;
  // ISRInternalGPIOPin input_isr_pin_;

  optional<uint8_t> source_{};
  uint8_t destination_;
  bool static_{false};
  std::function<std::vector<uint8_t>(Ts...)> data_func_{};
  std::vector<uint8_t> data_static_{};
};

class HdmiCecTrigger : public Trigger<std::vector<uint8_t>>, public Component {
  friend class HdmiCec;

 public:
  explicit HdmiCecTrigger(HdmiCec *parent)
      : parent_(parent){};
  void setup() override { this->parent_->add_trigger(this); }

 protected:
  HdmiCec *parent_;
  uint8_t source_;
  uint8_t destination_;
  uint8_t opcode_;
  std::vector<uint8_t> data_{};
};

}  // namespace hdmi_cec
}  // namespace esphome
