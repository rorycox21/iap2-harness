#include "transport/transport.hpp"
#include <cstdio>
namespace transport {
std::unique_ptr<Transport> make_libusb(const UsbConfig&) {
    std::fprintf(stderr, "USB transport disabled in this build; configure CARPLAY_ENABLE_USB=ON with target libusb\n");
    return {};
}
int list_usb_devices() {
    std::fprintf(stderr, "USB enumeration unavailable: CARPLAY_ENABLE_USB=OFF\n");
    return -1;
}
}
