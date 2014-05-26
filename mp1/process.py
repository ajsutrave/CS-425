#!/usr/bin/env python
from multiprocessing import Process, Queue
import sys

DELTA_MONEY_SIGN = 1
DELTA_MONEY = 2
DELTA_WIDGETS_SIGN = 4
DELTA_WIDGETS = 5

def my_process(my_proc_id, channels):
    def parse_message(m):
        message_list = m.split()
        d_m, d_w = int(message_list[DELTA_MONEY]), int(message_list[DELTA_WIDGETS])
        return d_m, d_w

    print "Proc%d money %d widgets %d" % (my_proc_id, money[my_proc_id], widgets[my_proc_id])
    for sender in xrange(NUM_PROC):
        if not channels[sender][my_proc_id].empty():
            if sender == my_proc_id:
                raise Exception("Sender and Receiver are the same!")
            message = channels[sender][my_proc_id].get()
            print "From %d to %d : %s" % ( sender, my_proc_id, message )
            delta_money, delta_widgets = parse_message(message)
            money[my_proc_id] += delta_money
            widgets[my_proc_id] += delta_widgets
            print money, widgets




if __name__ == '__main__':
    if len(sys.argv) != 2:
        print """Usage: process.py number_of_processes"""
        exit(1)
    else:
        NUM_PROC = int(sys.argv[1])

    channels = []
    for sender in xrange(NUM_PROC):
        channels.append([])
        for recvr in xrange(NUM_PROC):
            channels[sender].append(Queue())
    
    money = [ 100 for x in xrange(NUM_PROC) ]
    widgets = [ 10 for x in xrange(NUM_PROC) ]

    proc_list = range(NUM_PROC)
    for proc_id in xrange(NUM_PROC):
        proc_list[proc_id] = Process(target = my_process, 
                                     args   = (proc_id, channels) )
        proc_list[proc_id].start()

    def tick():
        for proc_id in xrange(NUM_PROC):
            proc_list[proc_id].run()


    channels[2][1].put("money + 10 widgets + 1")
    tick()

    for proc in proc_list:
        proc.join()
