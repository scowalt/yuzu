// Copyright 2014 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <chrono>
#include <thread>
#include <libusb.h>
#include "common/logging/log.h"
#include "input_common/gcadapter/gc_adapter.h"

namespace GCAdapter {

/// Used to loop through and assign button in poller
constexpr std::array<PadButton, 12> PadButtonArray{
    PadButton::PAD_BUTTON_LEFT, PadButton::PAD_BUTTON_RIGHT, PadButton::PAD_BUTTON_DOWN,
    PadButton::PAD_BUTTON_UP,   PadButton::PAD_TRIGGER_Z,    PadButton::PAD_TRIGGER_R,
    PadButton::PAD_TRIGGER_L,   PadButton::PAD_BUTTON_A,     PadButton::PAD_BUTTON_B,
    PadButton::PAD_BUTTON_X,    PadButton::PAD_BUTTON_Y,     PadButton::PAD_BUTTON_START,
};

Adapter::Adapter() {
    if (usb_adapter_handle != nullptr) {
        return;
    }
    LOG_INFO(Input, "GC Adapter Initialization started");

    current_status = NO_ADAPTER_DETECTED;
    get_origin.fill(true);

    const int init_res = libusb_init(&libusb_ctx);
    if (init_res == LIBUSB_SUCCESS) {
        StartScanThread();
    } else {
        LOG_ERROR(Input, "libusb could not be initialized. failed with error = {}", init_res);
    }
}

GCPadStatus Adapter::GetPadStatus(std::size_t port, const std::array<u8, 37>& adapter_payload) {
    GCPadStatus pad = {};

    ControllerTypes type = ControllerTypes(adapter_payload[1 + (9 * port)] >> 4);
    adapter_controllers_status[port] = type;

    static constexpr std::array<PadButton, 8> b1_buttons{
        PadButton::PAD_BUTTON_A,    PadButton::PAD_BUTTON_B,    PadButton::PAD_BUTTON_X,
        PadButton::PAD_BUTTON_Y,    PadButton::PAD_BUTTON_LEFT, PadButton::PAD_BUTTON_RIGHT,
        PadButton::PAD_BUTTON_DOWN, PadButton::PAD_BUTTON_UP,
    };

    static constexpr std::array<PadButton, 4> b2_buttons{
        PadButton::PAD_BUTTON_START,
        PadButton::PAD_TRIGGER_Z,
        PadButton::PAD_TRIGGER_R,
        PadButton::PAD_TRIGGER_L,
    };

    if (adapter_controllers_status[port] == ControllerTypes::None && !get_origin[port]) {
        // Controller may have been disconnected, recalibrate if reconnected.
        get_origin[port] = true;
    }

    if (adapter_controllers_status[port] != ControllerTypes::None) {
        const u8 b1 = adapter_payload[1 + (9 * port) + 1];
        const u8 b2 = adapter_payload[1 + (9 * port) + 2];

        for (std::size_t i = 0; i < b1_buttons.size(); ++i) {
            if ((b1 & (1U << i)) != 0) {
                pad.button |= static_cast<u16>(b1_buttons[i]);
            }
        }

        for (std::size_t j = 0; j < b2_buttons.size(); ++j) {
            if ((b2 & (1U << j)) != 0) {
                pad.button |= static_cast<u16>(b2_buttons[j]);
            }
        }

        pad.stick_x = adapter_payload[1 + (9 * port) + 3];
        pad.stick_y = adapter_payload[1 + (9 * port) + 4];
        pad.substick_x = adapter_payload[1 + (9 * port) + 5];
        pad.substick_y = adapter_payload[1 + (9 * port) + 6];
        pad.trigger_left = adapter_payload[1 + (9 * port) + 7];
        pad.trigger_right = adapter_payload[1 + (9 * port) + 8];

        if (get_origin[port]) {
            origin_status[port].stick_x = pad.stick_x;
            origin_status[port].stick_y = pad.stick_y;
            origin_status[port].substick_x = pad.substick_x;
            origin_status[port].substick_y = pad.substick_y;
            origin_status[port].trigger_left = pad.trigger_left;
            origin_status[port].trigger_right = pad.trigger_right;
            get_origin[port] = false;
        }
    }
    return pad;
}

void Adapter::PadToState(const GCPadStatus& pad, GCState& state) {
    for (const auto& button : PadButtonArray) {
        const u16 button_value = static_cast<u16>(button);
        state.buttons.insert_or_assign(button_value, pad.button & button_value);
    }

    state.axes.insert_or_assign(static_cast<u8>(PadAxes::StickX), pad.stick_x);
    state.axes.insert_or_assign(static_cast<u8>(PadAxes::StickY), pad.stick_y);
    state.axes.insert_or_assign(static_cast<u8>(PadAxes::SubstickX), pad.substick_x);
    state.axes.insert_or_assign(static_cast<u8>(PadAxes::SubstickY), pad.substick_y);
    state.axes.insert_or_assign(static_cast<u8>(PadAxes::TriggerLeft), pad.trigger_left);
    state.axes.insert_or_assign(static_cast<u8>(PadAxes::TriggerRight), pad.trigger_right);
}

void Adapter::Read() {
    LOG_DEBUG(Input, "GC Adapter Read() thread started");

    int payload_size_in, payload_size_copy;
    std::array<u8, 37> adapter_payload;
    std::array<u8, 37> adapter_payload_copy;
    std::array<GCPadStatus, 4> pads;

    while (adapter_thread_running) {
        libusb_interrupt_transfer(usb_adapter_handle, input_endpoint, adapter_payload.data(),
                                  sizeof(adapter_payload), &payload_size_in, 16);
        payload_size_copy = 0;
        // this mutex might be redundant?
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            std::copy(std::begin(adapter_payload), std::end(adapter_payload),
                      std::begin(adapter_payload_copy));
            payload_size_copy = payload_size_in;
        }

        if (payload_size_copy != sizeof(adapter_payload_copy) ||
            adapter_payload_copy[0] != LIBUSB_DT_HID) {
            LOG_ERROR(Input, "error reading payload (size: {}, type: {:02x})", payload_size_copy,
                      adapter_payload_copy[0]);
            adapter_thread_running = false; // error reading from adapter, stop reading.
            break;
        }
        for (std::size_t port = 0; port < pads.size(); ++port) {
            pads[port] = GetPadStatus(port, adapter_payload_copy);
            if (DeviceConnected(port) && configuring) {
                if (pads[port].button != 0) {
                    pad_queue[port].Push(pads[port]);
                }

                // Accounting for a threshold here because of some controller variance
                if (pads[port].stick_x > origin_status[port].stick_x + pads[port].THRESHOLD ||
                    pads[port].stick_x < origin_status[port].stick_x - pads[port].THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::StickX;
                    pads[port].axis_value = pads[port].stick_x;
                    pad_queue[port].Push(pads[port]);
                }
                if (pads[port].stick_y > origin_status[port].stick_y + pads[port].THRESHOLD ||
                    pads[port].stick_y < origin_status[port].stick_y - pads[port].THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::StickY;
                    pads[port].axis_value = pads[port].stick_y;
                    pad_queue[port].Push(pads[port]);
                }
                if (pads[port].substick_x > origin_status[port].substick_x + pads[port].THRESHOLD ||
                    pads[port].substick_x < origin_status[port].substick_x - pads[port].THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::SubstickX;
                    pads[port].axis_value = pads[port].substick_x;
                    pad_queue[port].Push(pads[port]);
                }
                if (pads[port].substick_y > origin_status[port].substick_y + pads[port].THRESHOLD ||
                    pads[port].substick_y < origin_status[port].substick_y - pads[port].THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::SubstickY;
                    pads[port].axis_value = pads[port].substick_y;
                    pad_queue[port].Push(pads[port]);
                }
                if (pads[port].trigger_left > pads[port].TRIGGER_THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::TriggerLeft;
                    pads[port].axis_value = pads[port].trigger_left;
                    pad_queue[port].Push(pads[port]);
                }
                if (pads[port].trigger_right > pads[port].TRIGGER_THRESHOLD) {
                    pads[port].axis = GCAdapter::PadAxes::TriggerRight;
                    pads[port].axis_value = pads[port].trigger_right;
                    pad_queue[port].Push(pads[port]);
                }
            }
            PadToState(pads[port], state[port]);
        }
        std::this_thread::yield();
    }
}

void Adapter::ScanThreadFunc() {
    LOG_INFO(Input, "GC Adapter scanning thread started");

    while (detect_thread_running) {
        if (usb_adapter_handle == nullptr) {
            std::lock_guard<std::mutex> lk(initialization_mutex);
            Setup();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void Adapter::StartScanThread() {
    if (detect_thread_running) {
        return;
    }
    if (!libusb_ctx) {
        return;
    }

    detect_thread_running = true;
    detect_thread = std::thread(&Adapter::ScanThreadFunc, this);
}

void Adapter::StopScanThread() {
    detect_thread_running = false;
    detect_thread.join();
}

void Adapter::Setup() {
    // Reset the error status in case the adapter gets unplugged
    if (current_status < 0) {
        current_status = NO_ADAPTER_DETECTED;
    }

    adapter_controllers_status.fill(ControllerTypes::None);

    // pointer to list of connected usb devices
    libusb_device** devices{};

    // populate the list of devices, get the count
    const ssize_t device_count = libusb_get_device_list(libusb_ctx, &devices);
    if (device_count < 0) {
        LOG_ERROR(Input, "libusb_get_device_list failed with error: {}", device_count);
        detect_thread_running = false; // Stop the loop constantly checking for gc adapter
        // TODO: For hotplug+gc adapter checkbox implementation, revert this.
        return;
    }

    if (devices != nullptr) {
        for (std::size_t index = 0; index < static_cast<std::size_t>(device_count); ++index) {
            if (CheckDeviceAccess(devices[index])) {
                // GC Adapter found and accessible, registering it
                GetGCEndpoint(devices[index]);
                break;
            }
        }
        libusb_free_device_list(devices, 1);
    }
    // Break out of the ScanThreadFunc() loop that is constantly looking for the device
    // Assumes user has GC adapter plugged in before launch to use the adapter
    detect_thread_running = false;
}

bool Adapter::CheckDeviceAccess(libusb_device* device) {
    libusb_device_descriptor desc;
    const int get_descriptor_error = libusb_get_device_descriptor(device, &desc);
    if (get_descriptor_error) {
        // could not acquire the descriptor, no point in trying to use it.
        LOG_ERROR(Input, "libusb_get_device_descriptor failed with error: {}",
                  get_descriptor_error);
        return false;
    }

    if (desc.idVendor != 0x057e || desc.idProduct != 0x0337) {
        // This isn't the device we are looking for.
        return false;
    }
    const int open_error = libusb_open(device, &usb_adapter_handle);

    if (open_error == LIBUSB_ERROR_ACCESS) {
        LOG_ERROR(Input, "Yuzu can not gain access to this device: ID {:04X}:{:04X}.",
                  desc.idVendor, desc.idProduct);
        return false;
    }
    if (open_error) {
        LOG_ERROR(Input, "libusb_open failed to open device with error = {}", open_error);
        return false;
    }

    int kernel_driver_error = libusb_kernel_driver_active(usb_adapter_handle, 0);
    if (kernel_driver_error == 1) {
        kernel_driver_error = libusb_detach_kernel_driver(usb_adapter_handle, 0);
        if (kernel_driver_error != 0 && kernel_driver_error != LIBUSB_ERROR_NOT_SUPPORTED) {
            LOG_ERROR(Input, "libusb_detach_kernel_driver failed with error = {}",
                      kernel_driver_error);
        }
    }

    if (kernel_driver_error && kernel_driver_error != LIBUSB_ERROR_NOT_SUPPORTED) {
        libusb_close(usb_adapter_handle);
        usb_adapter_handle = nullptr;
        return false;
    }

    const int interface_claim_error = libusb_claim_interface(usb_adapter_handle, 0);
    if (interface_claim_error) {
        LOG_ERROR(Input, "libusb_claim_interface failed with error = {}", interface_claim_error);
        libusb_close(usb_adapter_handle);
        usb_adapter_handle = nullptr;
        return false;
    }

    return true;
}

void Adapter::GetGCEndpoint(libusb_device* device) {
    libusb_config_descriptor* config = nullptr;
    const int config_descriptor_return = libusb_get_config_descriptor(device, 0, &config);
    if (config_descriptor_return != LIBUSB_SUCCESS) {
        LOG_ERROR(Input, "libusb_get_config_descriptor failed with error = {}",
                  config_descriptor_return);
        return;
    }

    for (u8 ic = 0; ic < config->bNumInterfaces; ic++) {
        const libusb_interface* interfaceContainer = &config->interface[ic];
        for (int i = 0; i < interfaceContainer->num_altsetting; i++) {
            const libusb_interface_descriptor* interface = &interfaceContainer->altsetting[i];
            for (u8 e = 0; e < interface->bNumEndpoints; e++) {
                const libusb_endpoint_descriptor* endpoint = &interface->endpoint[e];
                if (endpoint->bEndpointAddress & LIBUSB_ENDPOINT_IN) {
                    input_endpoint = endpoint->bEndpointAddress;
                } else {
                    output_endpoint = endpoint->bEndpointAddress;
                }
            }
        }
    }
    // This transfer seems to be responsible for clearing the state of the adapter
    // Used to clear the "busy" state of when the device is unexpectedly unplugged
    unsigned char clear_payload = 0x13;
    libusb_interrupt_transfer(usb_adapter_handle, output_endpoint, &clear_payload,
                              sizeof(clear_payload), nullptr, 16);

    adapter_thread_running = true;
    current_status = ADAPTER_DETECTED;
    adapter_input_thread = std::thread([=] { Read(); }); // Read input
}

Adapter::~Adapter() {
    StopScanThread();
    Reset();
}

void Adapter::Reset() {
    std::unique_lock<std::mutex> lock(initialization_mutex, std::defer_lock);
    if (!lock.try_lock()) {
        return;
    }
    if (current_status != ADAPTER_DETECTED) {
        return;
    }

    if (adapter_thread_running) {
        adapter_thread_running = false;
    }
    adapter_input_thread.join();

    adapter_controllers_status.fill(ControllerTypes::None);
    get_origin.fill(true);
    current_status = NO_ADAPTER_DETECTED;

    if (usb_adapter_handle) {
        libusb_release_interface(usb_adapter_handle, 1);
        libusb_close(usb_adapter_handle);
        usb_adapter_handle = nullptr;
    }

    if (libusb_ctx) {
        libusb_exit(libusb_ctx);
    }
}

bool Adapter::DeviceConnected(std::size_t port) {
    return adapter_controllers_status[port] != ControllerTypes::None;
}

void Adapter::ResetDeviceType(std::size_t port) {
    adapter_controllers_status[port] = ControllerTypes::None;
}

void Adapter::BeginConfiguration() {
    get_origin.fill(true);
    for (auto& pq : pad_queue) {
        pq.Clear();
    }
    configuring = true;
}

void Adapter::EndConfiguration() {
    for (auto& pq : pad_queue) {
        pq.Clear();
    }
    configuring = false;
}

std::array<Common::SPSCQueue<GCPadStatus>, 4>& Adapter::GetPadQueue() {
    return pad_queue;
}

const std::array<Common::SPSCQueue<GCPadStatus>, 4>& Adapter::GetPadQueue() const {
    return pad_queue;
}

std::array<GCState, 4>& Adapter::GetPadState() {
    return state;
}

const std::array<GCState, 4>& Adapter::GetPadState() const {
    return state;
}

int Adapter::GetOriginValue(int port, int axis) const {
    const auto& status = origin_status[port];

    switch (static_cast<PadAxes>(axis)) {
    case PadAxes::StickX:
        return status.stick_x;
    case PadAxes::StickY:
        return status.stick_y;
    case PadAxes::SubstickX:
        return status.substick_x;
    case PadAxes::SubstickY:
        return status.substick_y;
    case PadAxes::TriggerLeft:
        return status.trigger_left;
    case PadAxes::TriggerRight:
        return status.trigger_right;
    default:
        return 0;
    }
}

} // namespace GCAdapter
