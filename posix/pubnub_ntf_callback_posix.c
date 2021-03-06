/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#include "pubnub_ntf_callback.h"

#include "../posix/monotonic_clock_get_time.h"

#include "pubnub_internal.h"
#include "pubnub_assert.h"
#include "pubnub_log.h"
#include "pubnub_timer_list.h"
#include "pbpal.h"

#include "pbpal_ntf_callback_poller_poll.h"
#include "pbpal_ntf_callback_queue.h"
#include "pbpal_ntf_callback_handle_timer_list.h"

#include <pthread.h>

#include <stdlib.h>
#include <string.h>


struct SocketWatcherData {
    struct pbpal_poll_data* poll pubnub_guarded_by(mutw);
    pthread_mutex_t mutw;
    pthread_cond_t condw;
    pthread_t            thread_id;
#if PUBNUB_TIMERS_API
    pubnub_t* timer_head pubnub_guarded_by(mutw);
#endif
    struct pbpal_ntf_callback_queue queue;
};


static struct SocketWatcherData m_watcher;


static int elapsed_ms(struct timespec prev_timspec, struct timespec timspec)
{
    int s_diff   = timspec.tv_sec - prev_timspec.tv_sec;
    int m_s_diff = (timspec.tv_nsec - prev_timspec.tv_nsec) / MILLI_IN_NANO;
    return (s_diff * UNIT_IN_MILLI) + m_s_diff;
}


int pbntf_watch_in_events(pubnub_t* pbp)
{
    return pbpal_ntf_watch_in_events(m_watcher.poll, pbp);
}


int pbntf_watch_out_events(pubnub_t* pbp)
{
    return pbpal_ntf_watch_out_events(m_watcher.poll, pbp);
}


void* socket_watcher_thread(void* arg)
{
    struct timespec prev_timspec;
    monotonic_clock_get_time(&prev_timspec);

    for (;;) {
        struct timespec timspec;

        pbpal_ntf_callback_process_queue(&m_watcher.queue);

        monotonic_clock_get_time(&timspec);
        timspec.tv_sec += (timspec.tv_nsec + 200 * MILLI_IN_NANO) / UNIT_IN_NANO;
        timspec.tv_nsec = (timspec.tv_nsec + 200 * MILLI_IN_NANO) % UNIT_IN_NANO;

        pthread_mutex_lock(&m_watcher.mutw);

#if defined(__APPLE__)
        struct timespec relative_timspec = { .tv_sec  = 0,
                                             .tv_nsec = 200 * MILLI_IN_NANO };

        pthread_cond_timedwait_relative_np(
            &m_watcher.condw, &m_watcher.mutw, &relative_timspec);
#else
        pthread_cond_timedwait(&m_watcher.condw, &m_watcher.mutw, &timspec);
#endif

        pbpal_ntf_poll_away(m_watcher.poll, 100);

        if (PUBNUB_TIMERS_API) {
            int elapsed = elapsed_ms(prev_timspec, timspec);
            if (elapsed > 0) {
                pbntf_handle_timer_list(elapsed, &m_watcher.timer_head);
                prev_timspec = timspec;
            }
        }

        pthread_mutex_unlock(&m_watcher.mutw);
    }

    return NULL;
}


int pbntf_init(void)
{
    int                 rslt;
    pthread_mutexattr_t attr;

    rslt = pthread_mutexattr_init(&attr);
    if (rslt != 0) {
        PUBNUB_LOG_ERROR(
            "Failed to initialize mutex attributes, error code: %d", rslt);
        return -1;
    }
    rslt = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (rslt != 0) {
        PUBNUB_LOG_ERROR("Failed to set mutex attribute type, error code: %d",
                         rslt);
        pthread_mutexattr_destroy(&attr);
        return -1;
    }
    rslt = pthread_mutex_init(&m_watcher.mutw, &attr);
    if (rslt != 0) {
        PUBNUB_LOG_ERROR("Failed to initialize mutex, error code: %d", rslt);
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

#if defined(__APPLE__)
    rslt = pthread_cond_init(&m_watcher.condw, NULL);
#else
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    rslt = pthread_cond_init(&m_watcher.condw, &cond_attr);
#endif
    if (rslt != 0) {
        PUBNUB_LOG_ERROR(
            "Failed to initialize conditional variable, error code: %d", rslt);
        pthread_mutexattr_destroy(&attr);
        pthread_mutex_destroy(&m_watcher.mutw);
        return -1;
    }

    m_watcher.poll = pbpal_ntf_callback_poller_init();
    if (NULL == m_watcher.poll) {
        pthread_mutexattr_destroy(&attr);
        pthread_mutex_destroy(&m_watcher.mutw);
        return -1;
    }
    pbpal_ntf_callback_queue_init(&m_watcher.queue);
    
#if defined(PUBNUB_CALLBACK_THREAD_STACK_SIZE_KB)                              \
    && (PUBNUB_CALLBACK_THREAD_STACK_SIZE_KB > 0)
    {
        pthread_attr_t thread_attr;

        rslt = pthread_attr_init(&thread_attr);
        if (rslt != 0) {
            PUBNUB_LOG_ERROR(
                "Failed to initialize thread attributes, error code: %d\n", rslt);
            pthread_mutexattr_destroy(&attr);
            pthread_mutex_destroy(&m_watcher.mutw);
            pthread_mutex_destroy(&m_watcher.queue_lock);
            pbpal_ntf_callback_queue_deinit(&m_watcher.queue);
            pbpal_ntf_callback_poller_deinit(&m_watcher.poll);
            return -1;
        }
        rslt = pthread_attr_setstacksize(
            &thread_attr, PUBNUB_CALLBACK_THREAD_STACK_SIZE_KB * 1024);
        if (rslt != 0) {
            PUBNUB_LOG_ERROR(
                "Failed to set thread stack size to %d kb, error code: %d\n",
                PUBNUB_CALLBACK_THREAD_STACK_SIZE_KB,
                rslt);
            pthread_mutexattr_destroy(&attr);
            pthread_mutex_destroy(&m_watcher.mutw);
            pthread_mutex_destroy(&m_watcher.queue_lock);
            pthread_attr_destroy(&thread_attr);
            pbpal_ntf_callback_queue_deinit(&m_watcher.queue);
            pbpal_ntf_callback_poller_deinit(&m_watcher.poll);
            return -1;
        }
        rslt = pthread_create(
            &m_watcher.thread_id, &thread_attr, socket_watcher_thread, NULL);
        if (rslt != 0) {
            PUBNUB_LOG_ERROR(
                "Failed to create the polling thread, error code: %d\n", rslt);
            pthread_mutexattr_destroy(&attr);
            pthread_mutex_destroy(&m_watcher.mutw);
            pthread_mutex_destroy(&m_watcher.queue_lock);
            pthread_attr_destroy(&thread_attr);
            pbpal_ntf_callback_queue_deinit(&m_watcher.queue);
            pbpal_ntf_callback_poller_deinit(&m_watcher.poll);
            return -1;
        }
    }
#else
    rslt =
        pthread_create(&m_watcher.thread_id, NULL, socket_watcher_thread, NULL);
    if (rslt != 0) {
        PUBNUB_LOG_ERROR(
            "Failed to create the polling thread, error code: %d\n", rslt);
        pthread_mutexattr_destroy(&attr);
        pthread_mutex_destroy(&m_watcher.mutw);
        pbpal_ntf_callback_queue_deinit(&m_watcher.queue);
        pbpal_ntf_callback_poller_deinit(&m_watcher.poll);
        return -1;
    }
#endif

    return 0;
}


int pbntf_enqueue_for_processing(pubnub_t* pb)
{
    return pbpal_ntf_callback_enqueue_for_processing(&m_watcher.queue, pb);
}


int pbntf_requeue_for_processing(pubnub_t* pb)
{
    return pbpal_ntf_callback_requeue_for_processing(&m_watcher.queue, pb);
}


int pbntf_got_socket(pubnub_t* pb, pb_socket_t socket)
{
    pthread_mutex_lock(&m_watcher.mutw);

    pbpal_ntf_callback_save_socket(m_watcher.poll, pb);
    if (PUBNUB_TIMERS_API) {
        m_watcher.timer_head = pubnub_timer_list_add(m_watcher.timer_head, pb);
    }
    pthread_cond_signal(&m_watcher.condw);
    pthread_mutex_unlock(&m_watcher.mutw);

    return +1;
}


void pbntf_lost_socket(pubnub_t* pb, pb_socket_t socket)
{
    PUBNUB_UNUSED(socket);

    pthread_mutex_lock(&m_watcher.mutw);

    pbpal_ntf_callback_remove_socket(m_watcher.poll, pb);
    pbpal_remove_timer_safe(pb, &m_watcher.timer_head);
    pbpal_ntf_callback_remove_from_queue(&m_watcher.queue, pb);

    pthread_cond_signal(&m_watcher.condw);
    pthread_mutex_unlock(&m_watcher.mutw);
}


void pbntf_update_socket(pubnub_t* pb, pb_socket_t socket)
{
    PUBNUB_UNUSED(socket);

    pthread_mutex_lock(&m_watcher.mutw);

    pbpal_ntf_callback_update_socket(m_watcher.poll, pb);

    pthread_cond_signal(&m_watcher.condw);
    pthread_mutex_unlock(&m_watcher.mutw);
}
