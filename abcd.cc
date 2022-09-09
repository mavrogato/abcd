
#include <iostream>
#include <memory>
#include <string_view>
#include <filesystem>
#include <bit>
#include <complex>
#include <vector>
#include <numbers>


#include <CL/sycl.hpp>

#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "xdg-shell.h"

inline auto safe_ptr(wl_display* display) noexcept {
    return std::unique_ptr<wl_display, decltype (&wl_display_disconnect)>(display,
                                                                          wl_display_disconnect);
}
inline auto safe_ptr(wl_keyboard* keyboard) noexcept {
    return std::unique_ptr<wl_keyboard, decltype (&wl_keyboard_destroy)>(keyboard,
                                                                         wl_keyboard_destroy);
}
inline auto safe_ptr(wl_pointer* pointer) noexcept {
    return std::unique_ptr<wl_pointer, decltype (&wl_pointer_destroy)>(pointer,
                                                                       wl_pointer_destroy);
}
inline auto safe_ptr(wl_touch* touch) noexcept {
    return std::unique_ptr<wl_touch, decltype (&wl_touch_destroy)>(touch,
                                                                   wl_touch_destroy);
}
template <typename WL_CLIENT>
requires
std::is_same_v<WL_CLIENT, wl_registry>      ||
std::is_same_v<WL_CLIENT, wl_compositor>    ||
std::is_same_v<WL_CLIENT, wl_shell>         ||
std::is_same_v<WL_CLIENT, wl_seat>          ||
std::is_same_v<WL_CLIENT, wl_shm>           ||
std::is_same_v<WL_CLIENT, wl_surface>       ||
std::is_same_v<WL_CLIENT, wl_shell_surface> ||
std::is_same_v<WL_CLIENT, wl_shm_pool>      ||
std::is_same_v<WL_CLIENT, zxdg_shell_v6>    ||
std::is_same_v<WL_CLIENT, zxdg_surface_v6>  ||
std::is_same_v<WL_CLIENT, zxdg_toplevel_v6> ||
std::is_same_v<WL_CLIENT, wl_buffer>
inline auto safe_ptr(WL_CLIENT* ptr) noexcept {
    auto deleter = [](WL_CLIENT* ptr) noexcept {
        wl_proxy_destroy(reinterpret_cast<wl_proxy*>(ptr));
    };
    return std::unique_ptr<WL_CLIENT, decltype (deleter)>(ptr, deleter);
}

[[nodiscard]]
inline wl_buffer* create_shm_buffer(wl_shm* shm, size_t cx, size_t cy, uint32_t** pixels) noexcept {
    std::string_view xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir.empty() || !std::filesystem::exists(xdg_runtime_dir)) {
        std::cerr << "No XDG_RUNTIME_DIR settings..." << std::endl;
        return nullptr;
    }
    std::string_view tmp_file_title = "/weston-shared-XXXXXX";
    if (4096 <= xdg_runtime_dir.size() + tmp_file_title.size()) {
        std::cerr << "The path of XDG_RUNTIME_DIR is too long..." << std::endl;
        return nullptr;
    }
    char tmp_path[4096] = { };
    auto p = std::strcat(tmp_path, xdg_runtime_dir.data());
    std::strcat(p, tmp_file_title.data());
    int fd = mkostemp(tmp_path, O_CLOEXEC);
    if (fd >= 0) {
        unlink(tmp_path);
    }
    else {
        std::cerr << "mkostemp failed..." << std::endl;
        return nullptr;
    }
    if (ftruncate(fd, 4*cx*cy) < 0) {
        std::cerr << "ftruncate failed..." << std::endl;
        close(fd);
        return nullptr;
    }
    auto data = mmap(nullptr, 4*cx*cy, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        std::cerr << "mmap failed..." << std::endl;
        close(fd);
        return nullptr;
    }
    *pixels = reinterpret_cast<uint32_t*>(data);
    return wl_shm_pool_create_buffer(safe_ptr(wl_shm_create_pool(shm, fd, 4*cx*cy)).get(),
                                     0,
                                     cx, cy,
                                     cx*4,
                                     WL_SHM_FORMAT_XRGB8888);
}

int main() {
    auto display = safe_ptr(wl_display_connect(nullptr));
    if (!display) {
        std::cerr << "wl_display_connect failed..." << std::endl;
        return -1;
    }
    auto registry = safe_ptr(wl_display_get_registry(display.get()));
    if (!registry) {
        std::cerr << "wl_display_get_registry failed..." << std::endl;
        return -1;
    }
    auto compositor = safe_ptr<wl_compositor>(nullptr);
    auto shell = safe_ptr<zxdg_shell_v6>(nullptr);
    auto seat = safe_ptr<wl_seat>(nullptr);
    auto shm = safe_ptr<wl_shm>(nullptr);
    auto register_globals = [&](auto name, std::string_view interface, auto version) noexcept {
        if (interface == wl_compositor_interface.name) {
            compositor.reset(
                reinterpret_cast<wl_compositor*>(wl_registry_bind(registry.get(),
                                                                  name,
                                                                  &wl_compositor_interface,
                                                                  version)));
        }
        else if (interface == zxdg_shell_v6_interface.name) {
            shell.reset(reinterpret_cast<zxdg_shell_v6*>(wl_registry_bind(registry.get(),
                                                                          name,
                                                                          &zxdg_shell_v6_interface,
                                                                          version)));
        }
        else if (interface == wl_seat_interface.name) {
            seat.reset(reinterpret_cast<wl_seat*>(wl_registry_bind(registry.get(),
                                                                   name,
                                                                   &wl_seat_interface,
                                                                   version)));
        }
        else if (interface == wl_shm_interface.name) {
            shm.reset(reinterpret_cast<wl_shm*>(wl_registry_bind(registry.get(),
                                                                 name,
                                                                 &wl_shm_interface,
                                                                 version)));
        }
    };
    wl_registry_listener listener {
        .global = [](auto data, auto, auto... args) noexcept {
            (*reinterpret_cast<decltype (register_globals)*>(data))(args...);
        },
        .global_remove = [](auto...) noexcept {
            std::cerr << "Required global has been removed..." << std::endl;
            std::terminate();
        },
    };
    if (wl_registry_add_listener(registry.get(), &listener, &register_globals) != 0) {
        std::cerr << "wl_register_add_listener failed..." << std::endl;
        return -1;
    }
    if (wl_display_roundtrip(display.get()) == -1) {
        std::cerr << "wl_display_roundtrip failed..." << std::endl;
        return -1;
    }
    if (!compositor || !shell || !seat || !shm) {
        std::cerr << "Some required global not found..." << std::endl;
        return -1;
    }
    auto keyboard = safe_ptr(wl_seat_get_keyboard(seat.get()));
    if (!keyboard) {
        std::cerr << "wl_seat_get_keyboard failed..." << std::endl;
        return -1;
    }
    uint32_t key = 0;
    uint32_t state = 0;
    auto keyboard_key = [&](auto k, auto s) noexcept {
        key = k;
        state = s;
        if (k == 1 && s == 0) {
            exit(0);
        }
    };
    wl_keyboard_listener keyboard_listener {
        .keymap = [](auto...) noexcept { },
        .enter = [](auto...) noexcept { },
        .leave = [](auto...) noexcept { },
        .key = [](auto data, auto, auto, auto, auto... args) noexcept {
            (*reinterpret_cast<decltype (keyboard_key)*>(data))(args...);
        },
        .modifiers = [](auto...) noexcept { },
        .repeat_info = [](auto...) noexcept { },
    };
    if (wl_keyboard_add_listener(keyboard.get(), &keyboard_listener, &keyboard_key) != 0) {
        std::cerr << "wl_keyboard_add_listener failed..." << std::endl;
        return -1;
    }
    auto pointer = safe_ptr(wl_seat_get_pointer(seat.get()));
    std::vector<std::complex<double>> vertices;
    vertices.push_back({});
    auto pointer_motion = [&](auto x, auto y) noexcept {
        vertices.front() = { wl_fixed_to_double(x), wl_fixed_to_double(y) };
    };
    wl_pointer_listener pointer_listener = {
        .enter = [](auto...) noexcept { },
        .leave = [](auto...) noexcept { },
        .motion = [](auto data, auto, auto, auto... args) noexcept {
            (*reinterpret_cast<decltype (pointer_motion)*>(data))(args...);
        },
        .button = [](auto...) noexcept { },
        .axis = [](auto...) noexcept { },
        .frame = [](auto...) noexcept { },
        .axis_source = [](auto...) noexcept { },
        .axis_stop = [](auto...) noexcept { },
        .axis_discrete = [](auto...) noexcept { },
    };
    if (wl_pointer_add_listener(pointer.get(), &pointer_listener, &pointer_motion) != 0) {
        std::cerr << "wl_pointer_add_listener failed..." << std::endl;
        return -1;
    }
    auto surface = safe_ptr(wl_compositor_create_surface(compositor.get()));
    if (!surface) {
        std::cerr << "wl_compositor_create_surface failed..." << std::endl;
        return -1;
    }
    // auto shell_surface = safe_ptr(wl_shell_get_shell_surface(shell.get(), surface.get()));
    // if (!shell_surface) {
    //     std::cerr << "wl_shell_get_shell_surface failed..." << std::endl;
    //     return -1;
    // }
    // wl_shell_surface_listener shellsurf_listener {
    //     .ping = [](auto, auto shellsurf, auto serial) noexcept {
    //         wl_shell_surface_pong(shellsurf, serial);
    //     },
    //     .configure = [](auto...) noexcept { },
    //     .popup_done = [](auto...) noexcept { },
    // };
    // if (wl_shell_surface_add_listener(shell_surface.get(), &shellsurf_listener, nullptr) != 0) {
    //     std::cerr << "wl_shell_surface_add_listener failed..." << std::endl;
    //     return -1;
    // }
    // wl_shell_surface_set_toplevel(shell_surface.get());
    zxdg_shell_v6_listener shell_listener {
        .ping = [](auto, auto shell, auto serial) noexcept {
            zxdg_shell_v6_pong(shell, serial);
        },
    };
    if (zxdg_shell_v6_add_listener(shell.get(), &shell_listener, nullptr) != 0) {
        std::cerr << "xdg_shell_v6_add_listener failed..." << std::endl;
        return -1;
    }
    auto xdg_surface = safe_ptr(zxdg_shell_v6_get_xdg_surface(shell.get(), surface.get()));
    if (!xdg_surface) {
        std::cerr << "zxdg_shell_v6_get_xdg_surface failed..." << std::endl;
        return -1;
    }
    zxdg_surface_v6_listener xdg_surface_listener {
        .configure = [](auto, auto xdg_surface, auto serial) noexcept {
            zxdg_surface_v6_ack_configure(xdg_surface, serial);
        },
    };
    if (zxdg_surface_v6_add_listener(xdg_surface.get(), &xdg_surface_listener, nullptr) != 0) {
        std::cerr << "zxdg_surface_v6_add_listener failed..." << std::endl;
        return -1;
    }
    auto toplevel = safe_ptr(zxdg_surface_v6_get_toplevel(xdg_surface.get()));
    if (!toplevel) {
        std::cerr << "zxdg_surface_v6_get_toplevel failed..." << std::endl;
        return -1;
    }
    zxdg_toplevel_v6_listener toplevel_listener = {
        .configure = [](auto...) noexcept { },
        .close = [](auto...) noexcept { },
    };
    if (zxdg_toplevel_v6_add_listener(toplevel.get(), &toplevel_listener, nullptr) != 0) {
        std::cerr << "zxdg_toplevel_v6_add_listener failed..." << std::endl;
        return -1;
    }
    wl_surface_commit(surface.get());
    wl_display_roundtrip(display.get());
    constexpr size_t cx = 1280;
    constexpr size_t cy =  960;
    uint32_t* pixels = nullptr;
    auto buffer = safe_ptr(create_shm_buffer(shm.get(), cx, cy, &pixels));
    if (!buffer || !pixels) {
        std::cerr << "cannot create shm-buffer..." << std::endl;
        return -1;
    }
    sycl::queue que;
    do {
        constexpr double tau = std::numbers::pi * 2.0;
        constexpr double phi = std::numbers::phi;
        size_t N = 256 * 256;
        auto pix = sycl::buffer<uint8_t, 3>{reinterpret_cast<uint8_t*>(pixels), {cy, cx, 4}};
        auto vtx = sycl::buffer<std::complex<double>, 1>(&vertices.front(), vertices.size());
        que.submit([&](auto& h) noexcept {
            auto apix = pix.get_access<sycl::access::mode::write>(h);
            h.parallel_for({cy, cx, 4}, [=](auto idx) noexcept {
                // switch (idx[2]) {
                // case 0: apix[idx] = 0xff; break;
                // case 1:
                // case 2: apix[idx] = 0x00; break;
                // case 3: apix[idx] = 0xc0; break;
                // }
                apix[idx] = 0x00;
            });
        });
        que.submit([&](auto& h) noexcept {
            auto apix = pix.get_access<sycl::access::mode::write>(h);
            auto avtx = vtx.get_access<sycl::access::mode::read>(h);
            h.parallel_for({vertices.size(), N}, [=](auto idx) noexcept {
                auto n = idx[1];
                auto pt = avtx[idx[0]] + std::polar(sqrt(n), n*tau*phi);
                size_t y = pt.imag();
                size_t x = pt.real();
                apix[{y, x, 0}] = 0xff;
                apix[{y, x, 1}] = 0xff;
                apix[{y, x, 2}] = 0xff;
                apix[{y, x, 3}] = 0xff;
            });
        });
        wl_surface_damage(surface.get(), 0, 0, cx, cy);
        wl_surface_attach(surface.get(), buffer.get(), 0, 0);
        wl_surface_commit(surface.get());
        wl_display_flush(display.get());
    } while (wl_display_dispatch(display.get()) != -1);
    return 0;
}
