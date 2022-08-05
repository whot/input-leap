/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2004 Chris Schoeneman
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

#include <cassert>
#include "platform/EiEventQueueBuffer.h"

#include "mt/Thread.h"
#include "base/Event.h"
#include "base/Log.h"
#include "base/IEventQueue.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstdio>

class EventQueueTimer { };

EiEventQueueBuffer::EiEventQueueBuffer(struct ei *ei, IEventQueue* events) :
    m_events(events),
    m_ei(ei_ref(ei))
{
    // We need a pipe to signal ourselves when addEvent() is called
    int pipefd[2];
    int result = pipe(pipefd);
    assert(result == 0);

    int pipeflags;
    pipeflags = fcntl(pipefd[0], F_GETFL);
    fcntl(pipefd[0], F_SETFL, pipeflags | O_NONBLOCK);
    pipeflags = fcntl(pipefd[1], F_GETFL);
    fcntl(pipefd[1], F_SETFL, pipeflags | O_NONBLOCK);

    m_pipe_r = pipefd[0];
    m_pipe_w = pipefd[1];
}

EiEventQueueBuffer::~EiEventQueueBuffer()
{
    ei_unref(m_ei);
    close(m_pipe_r);
    close(m_pipe_w);
}

void
EiEventQueueBuffer::waitForEvent(double timeout_in_ms)
{
    Thread::testCancel();

    enum {
        EIFD,
        PIPEFD,
        POLLFD_COUNT, // Last element
    };

    struct pollfd pfds[POLLFD_COUNT];
    pfds[EIFD].fd       = ei_get_fd(m_ei);
    pfds[EIFD].events   = POLLIN;
    pfds[PIPEFD].fd     = m_pipe_r;
    pfds[PIPEFD].events = POLLIN;

    int timeout = (timeout_in_ms < 0.0) ? -1 :
                    static_cast<int>(1000.0 * timeout_in_ms);

    int retval = poll(pfds, POLLFD_COUNT, timeout);
    if (retval > 0) {
        if (pfds[EIFD].revents & POLLIN) {
            std::lock_guard<std::mutex> lock(mutex_);

            // libei doesn't allow ei_event_ref() because events are
            // supposed to be short-lived only. So instead, we create an NULL-data
            // kSystemEvent whenever there's data on the fd, shove that event
            // into our event queue and once we process the event (see
            // getEvent()), the EiScreen will call ei_dispatch() and process
            // all actual pending ei events. In theory this means that a
            // flood of ei events could starve the events added with
            // addEvents() but let's hope it doesn't come to that.
            m_queue.push({true, 0U});
        }
        // the pipefd data doesn't matter, it only exists to wake up the thread
        // and potentially testCancel
        if (pfds[PIPEFD].revents & POLLIN) {
            char buf[64];
            read(m_pipe_r, buf, sizeof(buf)); // discard
        }
    }
    Thread::testCancel();
}

IEventQueueBuffer::Type
EiEventQueueBuffer::getEvent(Event& event, uint32_t& dataID)
{
    // the addEvent/getEvent pair is a bit awkward for libei.
    //
    // it assumes that there's a nice queue of events sitting there that we can just
    // append to and get everything back out in the same order. We *could*
    // emulate that by taking the libei events immediately out of the event
    // queue after dispatch (see above) and putting it into the event queue,
    // intermixed with whatever addEvents() did.
    //
    // But this makes locking more awkward and libei isn't really designed to
    // keep calling ei_dispatch() while we hold a bunch of event refs. So instead
    // we just have a "something happened" event on the ei fd and the rest is
    // handled by the EiScreen.
    //
    std::lock_guard<std::mutex> lock(mutex_);
    auto pair = m_queue.front();
    m_queue.pop();

    // if this an injected special event, just return the data and exit
    if (pair.first == false) {
        dataID = pair.second;
        return kUser;
    }

    event = Event(Event::kSystem, m_events->getSystemTarget(), nullptr);

    return kSystem;
}

bool
EiEventQueueBuffer::addEvent(uint32_t dataID)
{
    std::lock_guard<std::mutex> lock(mutex_);
    m_queue.push({false, dataID});

    // tickle the pipe so our read thread wakes up
    write(m_pipe_w, "!", 1);

    return true;
}

bool
EiEventQueueBuffer::isEmpty() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return m_queue.empty();
}

EventQueueTimer*
EiEventQueueBuffer::newTimer(double, bool) const
{
    return new EventQueueTimer;
}

void
EiEventQueueBuffer::deleteTimer(EventQueueTimer* timer) const
{
    delete timer;
}
