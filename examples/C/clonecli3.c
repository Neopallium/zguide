//
//  Clone client model 3
//
//  Lets us 'build clonecli3' and 'build all'
#include "kvmsg.c"

int main (void) 
{
    //  Prepare our context and subscriber
    void *context = zmq_init (1);
    void *subscriber = zmq_socket (context, ZMQ_SUB);
    zmq_setsockopt (subscriber, ZMQ_SUBSCRIBE, "", 0);
    zmq_connect (subscriber, "tcp://localhost:5556");

    void *snapshot = zmq_socket (context, ZMQ_XREQ);
    zmq_connect (snapshot, "tcp://localhost:5557");

    void *updates = zmq_socket (context, ZMQ_PUSH);
    zmq_connect (updates, "tcp://localhost:5558");

    s_catch_signals ();
    zhash_t *kvmap = zhash_new ();
    srandom ((unsigned) time (NULL));
    
    //  Get state snapshot
    int64_t sequence = 0;
    s_send (snapshot, "I can haz state?");
    while (!s_interrupted) {
        kvmsg_t *kvmsg = kvmsg_recv (snapshot);
        if (!kvmsg)
            break;          //  Interrupted
        if (streq (kvmsg_key (kvmsg), "KTHXBAI")) {
            sequence = kvmsg_sequence (kvmsg);
            kvmsg_destroy (&kvmsg);
            break;          //  Done
        }
        kvmsg_store (&kvmsg, kvmap);
    }
    printf ("I: received snapshot=%" PRId64 "\n", sequence);
    int zero = 0;
    zmq_setsockopt (snapshot, ZMQ_LINGER, &zero, sizeof (zero));
    zmq_close (snapshot);

    int64_t alarm = s_clock () + 1000;
    while (!s_interrupted) {
        zmq_pollitem_t items [] = { { subscriber, 0, ZMQ_POLLIN, 0 } };
        int tickless = (int) ((alarm - s_clock ()));
        if (tickless < 0)
            tickless = 0;
        int rc = zmq_poll (items, 1, tickless * 1000);
        if (rc == -1)
            break;              //  Context has been shut down
        
        if (items [0].revents & ZMQ_POLLIN) {
            kvmsg_t *kvmsg = kvmsg_recv (subscriber);
            if (!kvmsg)
                break;          //  Interrupted

            //  Discard out-of-sequence kvmsgs, incl. heartbeats
            if (kvmsg_sequence (kvmsg) > sequence) {
                sequence = kvmsg_sequence (kvmsg);
                kvmsg_store (&kvmsg, kvmap);
                printf ("I: received update=%" PRId64 "\n", sequence);
            }
            else
                kvmsg_destroy (&kvmsg);
        }
        //  If we timed-out, generate a random kvmsg
        if (s_clock () >= alarm) {
            kvmsg_t *kvmsg = kvmsg_new (0);
            kvmsg_fmt_key  (kvmsg, "%d", randof (10000));
            kvmsg_fmt_body (kvmsg, "%d", randof (1000000));
            kvmsg_send (kvmsg, updates);
            kvmsg_destroy (&kvmsg);
            alarm = s_clock () + 1000;
        }
    }
    zhash_destroy (&kvmap);

    printf (" Interrupted\n%" PRId64 " messages in\n", sequence);
    zmq_setsockopt (updates, ZMQ_LINGER, &zero, sizeof (zero));
    zmq_close (updates);
    zmq_close (subscriber);
    zmq_term (context);
    return 0;
}
