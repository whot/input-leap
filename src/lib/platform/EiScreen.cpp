/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2002 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform/EiScreen.h"

#include "platform/EiEventQueueBuffer.h"
#include "platform/PortalRemoteDesktop.h"
#include "platform/EiKeyState.h"
#include "inputleap/Clipboard.h"
#include "inputleap/KeyMap.h"
#include "inputleap/XScreen.h"
#include "arch/XArch.h"
#include "arch/Arch.h"
#include "base/Log.h"
#include "base/Stopwatch.h"
#include "base/IEventQueue.h"
#include "base/TMethodEventJob.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <unistd.h>
#include <vector>

#include <libei.h>

//
// EiScreen
//
//
EiScreen::EiScreen(bool isPrimary, IEventQueue* events, bool usePortal) :
    PlatformScreen(events),
    m_isPrimary(isPrimary),
    m_events(events),
    m_keyState(NULL),
    m_ei(NULL),
    m_ei_seat(NULL),
    m_ei_pointer(NULL),
    m_ei_keyboard(NULL),
    m_ei_abs(NULL)
{
    // Server isn't supported yet
    assert(!isPrimary);

    m_ei = ei_new(NULL);
    ei_log_set_priority(m_ei, EI_LOG_PRIORITY_DEBUG);
    ei_configure_name(m_ei, "InputLeap client");

    m_keyState = new EiKeyState(this, events);
    // install event handlers
    m_events->adoptHandler(Event::kSystem, m_events->getSystemTarget(),
                            new TMethodEventJob<EiScreen>(this,
                                &EiScreen::handleSystemEvent));

    if (usePortal) {
        m_events->adoptHandler(m_events->forEiScreen().connectedToEIS(),
                               getEventTarget(),
                                new TMethodEventJob<EiScreen>(this,
                                    &EiScreen::handleConnectedToEISEvent));
        m_PortalRemoteDesktop = new PortalRemoteDesktop(this, m_events);
    } else {
        auto rc = ei_setup_backend_socket(m_ei, NULL);
        if (rc != 0) {
            LOG((CLOG_DEBUG "ei init error: %s", strerror(-rc)));
            throw XArch("Failed to init ei context");
        }
    }

    // install the platform event queue
    m_events->adoptBuffer(new EiEventQueueBuffer(this, m_ei, m_events));
}

EiScreen::~EiScreen()
{
    m_events->adoptBuffer(NULL);
    m_events->removeHandler(Event::kSystem, m_events->getSystemTarget());

    ei_device_unref(m_ei_pointer);
    ei_device_unref(m_ei_keyboard);
    ei_device_unref(m_ei_abs);
    ei_seat_unref(m_ei_seat);
    for (auto it = m_ei_devices.begin(); it != m_ei_devices.end(); it++)
        ei_device_unref(*it);
    m_ei_devices.clear();
    ei_unref(m_ei);

    delete m_PortalRemoteDesktop;
}

void*
EiScreen::getEventTarget() const
{
    return const_cast<EiScreen*>(this);
}

bool
EiScreen::getClipboard(ClipboardID id, IClipboard* clipboard) const
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
    return false;
}

void
EiScreen::getShape(int32_t& x, int32_t& y, int32_t& w, int32_t& h) const
{
    x = m_x;
    y = m_y;
    w = m_w;
    h = m_h;
}

void
EiScreen::getCursorPos(int32_t& x, int32_t& y) const
{
    // We cannot get the cursor position on EI, so we
    // always return the center of the screen
    x = m_x + m_w/2;
    y = m_y + m_y/2;
}

void
EiScreen::reconfigure(uint32_t)
{
    // do nothing
    assert(!"Not Supported");
}

void
EiScreen::warpCursor(int32_t x, int32_t y)
{
    // FIXME
    assert(!"Not Implemented");
}

uint32_t
EiScreen::registerHotKey(KeyID key, KeyModifierMask mask)
{
    // FIXME
    assert(!"Not Implemented");
    return 0;
}

void
EiScreen::unregisterHotKey(uint32_t id)
{
    // FIXME
    assert(!"Not Implemented");
}

void
EiScreen::fakeInputBegin()
{
    assert(!"Not Implemented");
    // FIXME -- not implemented
}

void
EiScreen::fakeInputEnd()
{
    assert(!"Not Implemented");
    // FIXME -- not implemented
}

int32_t
EiScreen::getJumpZoneSize() const
{
    assert(!"Not Implemented");
    return 1;
}

bool
EiScreen::isAnyMouseButtonDown(uint32_t& buttonID) const
{
    assert(!"Not Implemented");
    return false;
}

void
EiScreen::getCursorCenter(int32_t& x, int32_t& y) const
{
    x = m_x + m_w/2;
    y = m_y + m_y/2;
}

void
EiScreen::fakeMouseButton(ButtonID button, bool press)
{
    uint32_t code;

    if (!m_ei_pointer)
        return;

    switch (button) {
    case kButtonLeft:   code = 0x110; break; // BTN_LEFT
    case kButtonMiddle: code = 0x112; break; // BTN_MIDDLE
    case kButtonRight:  code = 0x111; break; // BTN_RIGHT
    default:
        code = 0x110 + (button - 1);
        break;
    }

    ei_device_pointer_button(m_ei_pointer, code, press);
    ei_device_frame(m_ei_pointer, ei_now(m_ei));
}

void
EiScreen::fakeMouseMove(int32_t x, int32_t y)
{
    if (!m_ei_abs)
        return;

    ei_device_pointer_motion_absolute(m_ei_abs, x, y);
    ei_device_frame(m_ei_abs, ei_now(m_ei));
}

void
EiScreen::fakeMouseRelativeMove(int32_t dx, int32_t dy) const
{
    if (!m_ei_pointer)
        return;

    ei_device_pointer_motion(m_ei_pointer, dx, dy);
    ei_device_frame(m_ei_abs, ei_now(m_ei));
}

void
EiScreen::fakeMouseWheel(int32_t xDelta, int32_t yDelta) const
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
}

void
EiScreen::fakeKey(uint32_t keycode, bool is_down) const
{
    ei_device_keyboard_key(m_ei_keyboard, keycode, is_down);
    ei_device_frame(m_ei_abs, ei_now(m_ei));
}

void
EiScreen::enable()
{
    // Nothing really to be done here
}

void
EiScreen::disable()
{
    // Nothing really to be done here, maybe cleanup in the future but ideally
    // that's handled elsewhere
}

void
EiScreen::enter()
{
    if (m_ei_pointer)
        ei_device_start_emulating(m_ei_pointer);
    if (m_ei_keyboard)
        ei_device_start_emulating(m_ei_keyboard);
    if (m_ei_abs)
        ei_device_start_emulating(m_ei_abs);
}

bool
EiScreen::leave()
{
    if (m_ei_pointer)
        ei_device_stop_emulating(m_ei_pointer);
    if (m_ei_keyboard)
        ei_device_stop_emulating(m_ei_keyboard);
    if (m_ei_abs)
        ei_device_stop_emulating(m_ei_abs);

    return true;
}

bool
EiScreen::setClipboard(ClipboardID id, const IClipboard* clipboard)
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
    return false;
}

void
EiScreen::checkClipboards()
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // do nothing, we're always up to date
}

void
EiScreen::openScreensaver(bool notify)
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
}

void
EiScreen::closeScreensaver()
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
}

void
EiScreen::screensaver(bool activate)
{
    printf("::::::::: %s:%d:%s() - \n", __FILE__, __LINE__, __func__);
    // FIXME
}

void
EiScreen::resetOptions()
{
    // Should reset options to neutral, see setOptions().
    // We don't have ei-specific options, nothing to do here
}

void
EiScreen::setOptions(const OptionsList& options)
{
    // We don't have ei-specific options, nothing to do here
}

void
EiScreen::setSequenceNumber(uint32_t seqNum)
{
    // FIXME: what is this used for?
}

bool
EiScreen::isPrimary() const
{
    return m_isPrimary;
}

void
EiScreen::updateShape()
{

    for (auto it = m_ei_devices.begin(); it != m_ei_devices.end(); it++) {
        auto idx = 0;
        struct ei_region *r;
        while ((r = ei_device_get_region(*it, idx++)) != NULL) {
            m_x = std::min(ei_region_get_x(r), m_x);
            m_y = std::min(ei_region_get_y(r), m_y);
            m_w = std::max(ei_region_get_x(r) + ei_region_get_width(r), m_w);
            m_h = std::max(ei_region_get_y(r) + ei_region_get_height(r), m_h);
        }
    }

    LOG((CLOG_NOTE "Logical output size: %dx%d@%d.%d", m_w, m_h, m_x, m_y));
}

void
EiScreen::addDevice(struct ei_device *device)
{
    LOG((CLOG_DEBUG "adding device %s", ei_device_get_name(device)));

    // Noteworthy: EI in principle supports multiple devices with multiple
    // capabilities, so there may be more than one logical pointer (or even
    // multiple seats). Supporting this is ... tricky so for now we go the easy
    // route: one device for each capability. Note this may be the same device
    // if the first device comes with multiple capabilities.

    if (!m_ei_pointer && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER))
        m_ei_pointer = ei_device_ref(device);

    if (!m_ei_keyboard && ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
        m_ei_keyboard = ei_device_ref(device);

        struct ei_keymap *keymap = ei_device_keyboard_get_keymap(device);
        if (keymap && ei_keymap_get_type(keymap) == EI_KEYMAP_TYPE_XKB) {
            int fd = ei_keymap_get_fd(keymap);
            size_t len = ei_keymap_get_size(keymap);
            m_keyState->init(fd, len);
        } else {
            // We rely on the EIS implementation to give us a keymap, otherwise we really have no
            // idea what a keycode means (other than it's linux/input.h code)
            // Where the EIS implementation does not tell us, we just default to
            // whatever libxkbcommon thinks is default. At least this way we can
            // influence with env vars what we get
            LOG((CLOG_WARN "keyboard device %s does not have a keymap, we are guessing", ei_device_get_name(device)));
            m_keyState->initDefaultKeymap();
        }
    }

    if (!m_ei_abs && ei_device_has_capability(device, EI_DEVICE_CAP_POINTER_ABSOLUTE))
        m_ei_abs = ei_device_ref(device);

    m_ei_devices.emplace_back(ei_device_ref(device));

    updateShape();
}

void
EiScreen::removeDevice(struct ei_device *device)
{
    LOG((CLOG_DEBUG "removing device %s", ei_device_get_name(device)));

    if (device == m_ei_pointer)
        m_ei_pointer = ei_device_unref(m_ei_pointer);
    if (device == m_ei_keyboard)
        m_ei_keyboard = ei_device_unref(m_ei_keyboard);
    if (device == m_ei_abs)
        m_ei_abs = ei_device_unref(m_ei_abs);

    for (auto it = m_ei_devices.begin(); it != m_ei_devices.end(); it++) {
        if (*it == device) {
            m_ei_devices.erase(it);
            ei_device_unref(device);
            break;
        }
    }

    updateShape();
}

void
EiScreen::handleConnectedToEISEvent(const Event& sysevent, void* data)
{
    int fd = *reinterpret_cast<int*>(sysevent.getData());
    LOG((CLOG_DEBUG "We have an EIS connection! fd is %d", fd));

    auto rc = ei_setup_backend_fd(m_ei, fd);
    if (rc != 0) {
        LOG((CLOG_NOTE "Failed to set up ei: %s", strerror(-rc)));
    }
}

void
EiScreen::handleSystemEvent(const Event& sysevent, void* data)
{
    std::lock_guard<std::mutex> lock(mutex_);

    // Only one ei_dispatch per system event, see the comment in
    // EiEventQueueBuffer::addEvent
    ei_dispatch(m_ei);
    struct ei_event * event;

    while ((event = ei_get_event(m_ei)) != nullptr) {
        auto type = ei_event_get_type(event);
        auto seat = ei_event_get_seat(event);
        auto device = ei_event_get_device(event);

        switch (type) {
            case EI_EVENT_CONNECT:
                LOG((CLOG_DEBUG "connected to EIS"));
                break;
            case EI_EVENT_SEAT_ADDED:
                if (!m_ei_seat) {
                    m_ei_seat = ei_seat_ref(seat);
                    ei_seat_bind_capability(m_ei_seat, EI_DEVICE_CAP_POINTER);
                    ei_seat_bind_capability(m_ei_seat, EI_DEVICE_CAP_POINTER_ABSOLUTE);
                    ei_seat_bind_capability(m_ei_seat, EI_DEVICE_CAP_KEYBOARD);
                    LOG((CLOG_DEBUG "using seat %s", ei_seat_get_name(m_ei_seat)));
                    // we don't care about touch
                }
                break;
            case EI_EVENT_DEVICE_ADDED:
                if (seat == m_ei_seat) {
                    addDevice(device);
                } else {
                    LOG((CLOG_INFO "seat %s is ignored", ei_seat_get_name(m_ei_seat)));
                }
                break;
            case EI_EVENT_DEVICE_REMOVED:
                removeDevice(device);
                break;
            case EI_EVENT_SEAT_REMOVED:
                if (seat == m_ei_seat)
                    m_ei_seat = ei_seat_unref(m_ei_seat);
                break;
            case EI_EVENT_DISCONNECT:
                throw XArch("Oops, EIS didn't like us");
            case EI_EVENT_DEVICE_PAUSED:
                LOG((CLOG_DEBUG "device %s is paused", ei_device_get_name(device)));
            case EI_EVENT_DEVICE_RESUMED:
                LOG((CLOG_DEBUG "device %s is resumed", ei_device_get_name(device)));
                break;
            case EI_EVENT_PROPERTY:
                LOG((CLOG_DEBUG "property %s: %s", ei_event_property_get_name(event),
                     ei_event_property_get_value(event)));
                break;
            case EI_EVENT_KEYBOARD_MODIFIERS:
                // FIXME
                break;

            // events below are for a receiver context (barriers)
            case EI_EVENT_FRAME:
            case EI_EVENT_DEVICE_START_EMULATING:
            case EI_EVENT_DEVICE_STOP_EMULATING:
            case EI_EVENT_KEYBOARD_KEY:
            case EI_EVENT_POINTER_BUTTON:
            case EI_EVENT_POINTER_MOTION:
            case EI_EVENT_POINTER_MOTION_ABSOLUTE:
            case EI_EVENT_TOUCH_UP:
            case EI_EVENT_TOUCH_MOTION:
            case EI_EVENT_TOUCH_DOWN:
            case EI_EVENT_POINTER_SCROLL:
            case EI_EVENT_POINTER_SCROLL_DISCRETE:
            case EI_EVENT_POINTER_SCROLL_STOP:
            case EI_EVENT_POINTER_SCROLL_CANCEL:
                break;
        }
        ei_event_unref(event);
    }
}

void
EiScreen::updateButtons()
{
    // libei relies on the EIS implementation to keep our button count correct,
    // so there's not much we need to/can do here.
}

IKeyState*
EiScreen::getKeyState() const
{
    return m_keyState;
}
