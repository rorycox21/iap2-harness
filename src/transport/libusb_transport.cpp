#include "transport/transport.hpp"
#include <libusb-1.0/libusb.h>
#include <cstdio>
#include <cstring>

namespace transport {
namespace {

class LibusbTransport : public Transport {
public:
    explicit LibusbTransport(UsbConfig cfg) : cfg_(cfg) {}
    ~LibusbTransport() override { close(); }

    bool open() override {
        if (libusb_init(&ctx_) != 0) {
            std::fprintf(stderr, "transport: libusb_init failed\n");
            return false;
        }
        libusb_device** list = nullptr;
        const ssize_t n = libusb_get_device_list(ctx_, &list);
        if (n < 0) { std::fprintf(stderr, "transport: get_device_list failed\n"); return false; }

        bool found = false;
        for (ssize_t i = 0; i < n && !found; ++i) {
            libusb_device_descriptor d{};
            if (libusb_get_device_descriptor(list[i], &d) != 0) continue;
            if (d.idVendor != cfg_.vid || d.idProduct != cfg_.pid) continue;
            int r = libusb_open(list[i], &handle_);
            if (r != 0) {
                std::fprintf(stderr, "transport: libusb_open(%04x:%04x) failed: %s\n",
                             cfg_.vid, cfg_.pid, libusb_error_name(r));
                continue;
            }
            found = true;
        }
        libusb_free_device_list(list, 1);
        if (!found) {
            std::fprintf(stderr, "transport: no device %04x:%04x\n",
                         cfg_.vid, cfg_.pid);
            return false;
        }

        // macOS: don't auto-detach; the kernel driver claiming the
        // interface is usbmuxd, and detaching it will break Finder/iTunes.
        // Fail with a clear message instead.
        // iPhone exposes 6 USB configurations. The iAP2 accessory pipe
        // (class 0xff / sub 0xfe / proto 0x02, bulk 0x04 out / 0x85 in)
        // is on configuration 4 (and 5). The active config is usually 6,
        // which is the USB MUX. Switch to config 4 if needed.
        int active_cfg = 0;
        libusb_get_configuration(handle_, &active_cfg);
        if (active_cfg != 4) {
            std::fprintf(stderr, "transport: switching config %d -> 4\n", active_cfg);
            int sr = libusb_set_configuration(handle_, 4);
            if (sr != 0) {
                std::fprintf(stderr,
                    "transport: set_configuration(4) failed: %s\n",
                    libusb_error_name(sr));
                // Non-fatal; try the claim anyway.
            }
        }
        // Use interface 1 (vendor-specific iAP2), not 0 (USB MUX).
        const int iface = 1;
        int r = libusb_claim_interface(handle_, iface);
        if (r == LIBUSB_ERROR_BUSY) {
            std::fprintf(stderr,
                "transport: interface %d busy. On macOS this is usually "
                "usbmuxd. Try: sudo pkill usbmuxd\n", cfg_.interface_number);
            return false;
        }
        if (r != 0) {
            std::fprintf(stderr, "transport: claim_interface(%d) failed: %s\n",
                         cfg_.interface_number, libusb_error_name(r));
            return false;
        }
        std::fprintf(stderr, "transport: opened %04x:%04x if=%d ep_in=0x%02x ep_out=0x%02x\n",
                     cfg_.vid, cfg_.pid, cfg_.interface_number, cfg_.ep_in, cfg_.ep_out);
        return true;
    }

    int write(const std::uint8_t* data, std::size_t n) override {
        int sent = 0;
        int r = libusb_bulk_transfer(handle_, cfg_.ep_out,
                                     const_cast<std::uint8_t*>(data),
                                     static_cast<int>(n), &sent, 1000);
        if (r != 0) {
            std::fprintf(stderr, "transport: bulk OUT failed: %s\n", libusb_error_name(r));
            return -1;
        }
        return sent;
    }

    int read(std::uint8_t* data, std::size_t n, int timeout_ms) override {
        int got = 0;
        int r = libusb_bulk_transfer(handle_, cfg_.ep_in, data,
                                     static_cast<int>(n), &got, timeout_ms);
        if (r == LIBUSB_ERROR_TIMEOUT) return 0;
        if (r != 0) {
            std::fprintf(stderr, "transport: bulk IN failed: %s\n", libusb_error_name(r));
            return -1;
        }
        return got;
    }

    void close() override {
        if (handle_) {
            libusb_release_interface(handle_, 1);
            libusb_close(handle_);
            handle_ = nullptr;
        }
        if (ctx_) { libusb_exit(ctx_); ctx_ = nullptr; }
    }

private:
    UsbConfig cfg_;
    libusb_context* ctx_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
};

}  // namespace

std::unique_ptr<Transport> make_libusb(const UsbConfig& cfg) {
    return std::make_unique<LibusbTransport>(cfg);
}

int list_usb_devices() {
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0) { std::fprintf(stderr, "libusb_init failed\n"); return -1; }
    libusb_device** list = nullptr;
    const ssize_t n = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) != 0) continue;
        char product[128] = "";
        char manufacturer[128] = "";
        libusb_device_handle* h = nullptr;
        if (libusb_open(list[i], &h) == 0) {
            if (d.iProduct && libusb_get_string_descriptor_ascii(
                    h, d.iProduct, reinterpret_cast<unsigned char*>(product), sizeof product) < 0)
                product[0] = 0;
            if (d.iManufacturer && libusb_get_string_descriptor_ascii(
                    h, d.iManufacturer, reinterpret_cast<unsigned char*>(manufacturer), sizeof manufacturer) < 0)
                manufacturer[0] = 0;
            libusb_close(h);
        }
        std::printf("%04x:%04x  %s  %s\n", d.idVendor, d.idProduct,
                    manufacturer[0] ? manufacturer : "(no manufacturer)",
                    product[0] ? product : "(no product)");
    }
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return static_cast<int>(n);
}

}  // namespace transport
