#include "wayland.h"

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace hm {

namespace {

const wl_registry_listener kRegistryListener = {
    .global = Wayland::on_global,
    .global_remove = Wayland::on_global_remove,
};

const wl_output_listener kOutputListener = {
    .geometry = Wayland::on_output_geometry,
    .mode = Wayland::on_output_mode,
    .done = Wayland::on_output_done,
    .scale = Wayland::on_output_scale,
    .name = Wayland::on_output_name,
    .description = Wayland::on_output_description,
};

}  // namespace

Wayland::Wayland() {
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        throw std::runtime_error("wl_display_connect failed (is WAYLAND_DISPLAY set?)");
    }

    registry_ = wl_display_get_registry(display_);
    wl_registry_add_listener(registry_, &kRegistryListener, this);

    wl_display_roundtrip(display_);  // bind globals
    wl_display_roundtrip(display_);  // collect output property events

    if (!compositor_) {
        throw std::runtime_error("wl_compositor missing");
    }
    if (!layer_shell_) {
        throw std::runtime_error("zwlr_layer_shell_v1 missing — compositor must support wlr-layer-shell");
    }
}

Wayland::~Wayland() {
    for (auto& o : outputs_) {
        if (o.proxy) wl_output_destroy(o.proxy);
    }
    if (layer_shell_) zwlr_layer_shell_v1_destroy(layer_shell_);
    if (compositor_) wl_compositor_destroy(compositor_);
    if (registry_) wl_registry_destroy(registry_);
    if (display_) wl_display_disconnect(display_);
}

void Wayland::roundtrip() {
    wl_display_roundtrip(display_);
}

int Wayland::dispatch() {
    return wl_display_dispatch(display_);
}

void Wayland::flush() {
    wl_display_flush(display_);
}

void Wayland::on_global(void* data, wl_registry* reg, uint32_t id,
                        const char* iface, uint32_t version) {
    auto* self = static_cast<Wayland*>(data);
    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        self->compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(reg, id, &wl_compositor_interface, std::min(version, 6u)));
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
        auto& out = self->outputs_.emplace_back();
        out.id = id;
        out.proxy = static_cast<wl_output*>(
            wl_registry_bind(reg, id, &wl_output_interface, std::min(version, 4u)));
        wl_output_add_listener(out.proxy, &kOutputListener, &out);
    } else if (std::strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        self->layer_shell_ = static_cast<zwlr_layer_shell_v1*>(
            wl_registry_bind(reg, id, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
    }
}

void Wayland::on_global_remove(void* data, wl_registry*, uint32_t id) {
    auto* self = static_cast<Wayland*>(data);
    auto it = std::find_if(self->outputs_.begin(), self->outputs_.end(),
                           [&](const Output& o) { return o.id == id; });
    if (it != self->outputs_.end()) {
        if (it->proxy) wl_output_destroy(it->proxy);
        self->outputs_.erase(it);
    }
}

void Wayland::on_output_geometry(void*, wl_output*, int32_t, int32_t, int32_t,
                                 int32_t, int32_t, const char*, const char*, int32_t) {}

void Wayland::on_output_mode(void* data, wl_output*, uint32_t flags,
                             int32_t w, int32_t h, int32_t) {
    auto* o = static_cast<Output*>(data);
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        o->width = w;
        o->height = h;
    }
}

void Wayland::on_output_done(void*, wl_output*) {}

void Wayland::on_output_scale(void* data, wl_output*, int32_t scale) {
    static_cast<Output*>(data)->scale = scale;
}

void Wayland::on_output_name(void* data, wl_output*, const char* name) {
    static_cast<Output*>(data)->name = name;
}

void Wayland::on_output_description(void* data, wl_output*, const char* desc) {
    static_cast<Output*>(data)->description = desc;
}

}  // namespace hm
