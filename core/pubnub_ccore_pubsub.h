/* -*- c-file-style:"stroustrup"; indent-tabs-mode: nil -*- */
#if !defined INC_PUBNUB_CCORE_PUBSUB
#define      INC_PUBNUB_CCORE_PUBSUB

#include "pubnub_config.h"
#include "pubnub_api_types.h"

#include <stdbool.h>
#include <stdlib.h>


/** @file pubnub_ccore_pubsub.h 

    This has the functions for formating and parsing the requests and
    responses for publish and subscribe transactions.
*/


/** The Pubnub "(C) core" context, contains context data 
    that is shared among all Pubnub C clients.
 */
struct pbcc_context {
    /** The publish key (to use when publishing) */
    char const *publish_key;
    /** The subscribe key (to use when subscribing) */
    char const *subscribe_key;
    /** The UUID to be sent to server. If NULL, don't send any */
    char const *uuid;
    /** The `auth` parameter to be sent to server. If NULL, don't send
     * any */
    char const *auth;

    /** The last used time token. */
    char timetoken[20];

    /** The result of the last Pubnub transaction */
    enum pubnub_res last_result;

    /** The "scratch" buffer for HTTP data */
    char http_buf[PUBNUB_BUF_MAXLEN];

    /** The length of the data currently in the HTTP buffer ("scratch"
        or reply, depending on the state).
     */
    unsigned http_buf_len;

    /** The total length of data to be received in a HTTP reply or
        chunk of it.
     */
    unsigned http_content_len;

#if PUBNUB_DYNAMIC_REPLY_BUFFER
    char *http_reply;
#else
    /** The contents of a HTTP reply/reponse */
    char http_reply[PUBNUB_REPLY_MAXLEN+1];
#endif

    /* These in-string offsets are used for yielding messages received
     * by subscribe - the beginning of last yielded message and total
     * length of message buffer.
     */
    unsigned msg_ofs, msg_end;

    /* Like the offsets for the messages, these are the offsets for
       the channels. Unlikey the message(s), the channels don't have
       to be received at all.
    */
    unsigned chan_ofs, chan_end;

#if PUBNUB_CRYPTO_API
    /** Secret key to use for encryption/decryption */
    char const *secret_key;
#endif
};



#define APPEND_URL_PARAM_M(pbc, name, var, separator)                   \
    if ((var) != NULL) {                                                \
        const char param_[] = name;                                 	\
        enum pubnub_res rslt_ = pbcc_append_url_param((pbc), param_, sizeof param_ - 1, (var), (separator)); \
        if (rslt_ != PNR_OK) {                                          \
            return rslt_;                                               \
        }                                                               \
    }


#define APPEND_URL_PARAM_INT_M(pbc, name, var, separator)               \
    do { char v_[20];                                                   \
        snprintf(v_, sizeof v_, "%d", (var));                           \
        APPEND_URL_PARAM_M(pbc, name, v_, separator);                   \
    } while (0)


#define APPEND_URL_PARAM_UNSIGNED_M(pbc, name, var, separator)          \
    do { char v_[20];                                                   \
        snprintf(v_, sizeof v_, "%u", (var));                           \
        APPEND_URL_PARAM_M(pbc, name, v_, separator);                   \
    } while (0)

#define APPEND_URL_OPT_PARAM_UNSIGNED_M(pbc, name, var, separator)      \
    if ((var) != NULL) { char v_[20];                                   \
        snprintf(v_, sizeof v_, "%u", *(var));                          \
        APPEND_URL_PARAM_M(pbc, name, v_, separator);                   \
    } while (0)


#define APPEND_URL_PARAM_BOOL_M(pbc, name, var, separator)              \
    do { char const *v_ = (var) ? "true" : "false";                     \
        APPEND_URL_PARAM_M(pbc, name, v_, separator);                   \
    } while (0)

#define APPEND_URL_PARAM_TRIBOOL_M(pbc, name, var, separator)           \
    if ((var) != pbccNotSet) {                                          \
        char const *v_ = (var) ? "1" : "0";                             \
        APPEND_URL_PARAM_M(pbc, name, v_, separator);                   \
    }



/** Initializes the Pubnub C core context */
void pbcc_init(struct pbcc_context *pbcc, const char *publish_key, const char *subscribe_key);

/** Deinitializes the Pubnub C core context */
void pbcc_deinit(struct pbcc_context *p);

/** Reallocates the reply buffer in the C core context @p p to have
    @p bytes.
    @return 0: OK, allocated, -1: failed
*/
int pbcc_realloc_reply_buffer(struct pbcc_context *p, unsigned bytes);

/** Returns the next message from the Pubnub C Core context. NULL if
    there are no (more) messages
*/
char const *pbcc_get_msg(struct pbcc_context *pb);

/** Returns the next channel from the Pubnub C Core context. NULL if
    there are no (more) messages.
*/
char const *pbcc_get_channel(struct pbcc_context *pb);

/** Sets the UUID for the context */
void pbcc_set_uuid(struct pbcc_context *pb, const char *uuid);

/** Sets the `auth` for the context */
void pbcc_set_auth(struct pbcc_context *pb, const char *auth);

/** Response parser function prototype */
typedef enum pubnub_res (*PFpbcc_parse_response_T)(struct pbcc_context *);

/** Parses the string received as a response for a subscribe operation
    (transaction). This checks if the response is valid, and, if it
    is, prepares for giving the messages (and possibly channels) that
    are received in the response to the user (via pbcc_get_msg() and
    pbcc_get_channel()).

    @param p The Pubnub C core context to parse the response "in"
    @return 0: OK, -1: error (invalid response)
*/
enum pubnub_res pbcc_parse_subscribe_response(struct pbcc_context *p);

/** Parses the string received as a response for a publish operation
    (transaction). This checks if the response is valid, and, if it
    is, enables getting it as the gotten message (like for
    `subscribe`).

    @param p The Pubnub C core context to parse the response "in"
    @return The result of the parsing, expressed as the "Pubnub
    result" enum
*/
enum pubnub_res pbcc_parse_publish_response(struct pbcc_context *p);


/** Prepares the Publish operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_publish_prep(struct pbcc_context *pb, const char *channel, const char *message, bool store_in_history, bool eat_after_reading);

/** Prepares the Subscribe operation (transaction), mostly by
    formatting the URI of the HTTP request.
 */
enum pubnub_res pbcc_subscribe_prep(struct pbcc_context *p, const char *channel, const char *channel_group, unsigned *heartbeat);



/** Split @p buf string containing a JSON array (with arbitrary
 * contents) to multiple NUL-terminated C strings, in-place.
 */
bool pbcc_split_array(char *buf);


enum pubnub_res pbcc_append_url_param(struct pbcc_context *pb, char const *param_name, size_t param_name_len, char const *param_val, char separator);


#endif /* !defined INC_PUBNUB_CCORE_PUBSUB */
